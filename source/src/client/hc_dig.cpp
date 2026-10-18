// client.dll: the cells minecraft dug out of half-life's walls (see hc_dig.h).

#include "cbase.h"
#include "engine/IEngineTrace.h"
#include "view.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

#include "halfcraft_protocol.h"
#include "client/hc_dig.h"
#include "client/hc_texture.h"
#include "core/hc_dig_mask.h"
#include "core/hc_dug_cells.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "core/hc_units.h"
#include "shared/hc_bridge.h"
#include "shared/hc_dig_abi.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int   SECTION_BLOCKS = 16;
		constexpr int   TEXEL_BYTES = 4;  // the dig mask's rgba
		constexpr float CROSSHAIR_REACH = 180.0f;  // units hc_debug_dig_cell looks for a wall in (4.5 blocks: minecraft's reach)
		constexpr float INTO_WALL = 0.5f;          // units behind the face under the crosshair: in the cell the face belongs to

		ConVar hc_dig("hc_dig", "1", FCVAR_ARCHIVE, "halfcraft: 0 - minecraft digs no new cells out of half-life's walls (the ones dug stay dug)");
		ConVar hc_debug_dig_mask("hc_debug_dig_mask", "0", 0,
			"halfcraft: log each time the dig mask moves to the player: laid out and uploaded again, or its layout alone (no cell dug in it before or after)");

		// r_novis and r_portalsopenall as they were before force_vis turned them on
		bool g_vis_forced = false;
		int  g_old_novis = 0;
		int  g_old_portals = 0;

		bool g_holes_near = false;  // dig_holes_near's, as dig_update last found it

		using SectionKey = std::tuple<int, int, int>;  // a section, minecraft section coords

		struct DigStore
		{
			std::mutex lock;
			// every section a cell was dug in, with the version it last changed at (emptied ones stay, so server.dll
			// hears they're gone)
			std::map<SectionKey, std::pair<DugSection, std::uint32_t>> sections;
			std::uint32_t                                              version = 0;
			MapSlot                                                    slot;
		};

		DigStore& store()
		{
			static DigStore instance;
			return instance;
		}

		MapSlot current_slot()
		{
			auto&                       d = store();
			std::lock_guard<std::mutex> lock(d.lock);
			return d.slot;
		}

		/// the dug cells around the player as the shader dll samples them (core/hc_dig_mask.h; main thread)
		struct DigMask
		{
			McTexture                 texture{ "dig_mask" };
			std::vector<std::uint8_t> rgba;  // the texture's image
			DigMaskLayout             layout;
			bool                      built = false;
			bool                      dug = false;  // the image may have a texel dug (mask_needs_rebuild's image_dug)
			std::uint32_t             version = 0;  // the store's, as the mask has it
			DigMaskState              state{};
			std::vector<DugSection>   sections;  // dug_since's, kept for the next call
		};

		DigMask& mask()
		{
			static DigMask instance;
			return instance;
		}

		/// one section's mask (from minecraft, or hc_debug_dig_cell). the one it has already changes nothing (no new
		/// version): kRenDug comes with every kRenSection.
		/// @param bits - nullptr: nothing dug there
		void set_dug(std::int32_t sx, std::int32_t sy, std::int32_t sz, std::uint32_t count, const std::uint8_t* bits)
		{
			auto&                       d = store();
			std::lock_guard<std::mutex> lock(d.lock);
			const SectionKey            key{ sx, sy, sz };
			const auto                  it = d.sections.find(key);
			if (!bits) {
				if (it == d.sections.end() || it->second.first.count == 0) {
					return;  // nothing was dug there, and nothing is
				}
			} else if (it != d.sections.end() && it->second.first.count == count && std::memcmp(it->second.first.bits, bits, sizeof(it->second.first.bits)) == 0) {
				return;
			}
			auto&       entry = it != d.sections.end() ? it->second : d.sections[key];
			DugSection& section = entry.first;
			section.sx = sx;
			section.sy = sy;
			section.sz = sz;
			section.count = bits ? count : 0;
			if (bits) {
				std::memcpy(section.bits, bits, sizeof(section.bits));
			} else {
				std::memset(section.bits, 0, sizeof(section.bits));
			}
			entry.second = ++d.version;
		}

		/// empties the sections with cells dug that pick(key) picks
		template <class Pick>
		void empty_sections(Pick&& pick)
		{
			auto&                       d = store();
			std::lock_guard<std::mutex> lock(d.lock);
			for (auto& entry : d.sections) {
				DugSection& section = entry.second.first;
				if (section.count > 0 && pick(entry.first)) {
					section.count = 0;
					std::memset(section.bits, 0, sizeof(section.bits));
					entry.second.second = ++d.version;
				}
			}
		}

		/// whether a minecraft block's cell is dug (the store's lock held)
		bool cell_dug(const DigStore& d, int x, int y, int z)
		{
			const auto it = d.sections.find(SectionKey{ dug_detail::section_of(x), dug_detail::section_of(y), dug_detail::section_of(z) });
			if (it == d.sections.end() || it->second.first.count == 0) {
				return false;
			}
			const int bit = dug_detail::bit_of(x, y, z);
			return ((it->second.first.bits[bit >> 3] >> (bit & 7)) & 1) != 0;
		}

		/// whether a cell is dug in a section the dig mask's layout has (the ones it cuts out; dig_set_slot has emptied
		/// the sections of other maps' slots)
		bool any_dug_in(const DigMaskLayout& layout)
		{
			auto&                       d = store();
			std::lock_guard<std::mutex> lock(d.lock);
			return std::any_of(d.sections.begin(), d.sections.end(), [&layout](const auto& entry) {
				const DugSection& section = entry.second.first;
				return section.count > 0 && layout.has_section(section.sx, section.sy, section.sz);
			});
		}

		/// sets a cvar to value unless it has it already (a missing one is left alone)
		void set_cvar(ConVarRef& ref, int value)
		{
			if (ref.IsValid() && ref.GetInt() != value) {
				ref.SetValue(value);
			}
		}

		/// want: r_novis 1 and r_portalsopenall 1, so the rooms behind the holes near the player are drawn (the map's
		/// vis hides them behind its walls); otherwise their values from before come back. both are cheats:
		/// ConVar::SetValue doesn't check FCVAR_CHEAT (the flag stays on them), but the engine puts cheats back to their
		/// defaults when sv_cheats changes, so they're set again every frame.
		void force_vis(bool want)
		{
			if (!want && !g_vis_forced) {
				return;
			}
			static ConVarRef novis("r_novis"), portals("r_portalsopenall");
			if (want) {
				if (!g_vis_forced) {
					g_old_novis = novis.IsValid() ? novis.GetInt() : 0;
					g_old_portals = portals.IsValid() ? portals.GetInt() : 0;
					g_vis_forced = true;
					log_info("dig: this map has holes: r_novis 1, r_portalsopenall 1 (they were %d, %d)", g_old_novis, g_old_portals);
				}
				set_cvar(novis, 1);
				set_cvar(portals, 1);
			} else {
				set_cvar(novis, g_old_novis);
				set_cvar(portals, g_old_portals);
				g_vis_forced = false;
				log_info("dig: no holes here: r_novis %d, r_portalsopenall %d again", g_old_novis, g_old_portals);
			}
		}

		/// hc_debug_dig_cell: one cell of its section's mask dug or filled, as if minecraft had sent it so
		void debug_dig_cell(const int cell[3], bool dig)
		{
			const int    sx = dug_detail::section_of(cell[0]), sy = dug_detail::section_of(cell[1]), sz = dug_detail::section_of(cell[2]);
			std::uint8_t bits[sizeof(DugSection::bits)] = {};
			{
				auto&                       d = store();
				std::lock_guard<std::mutex> lock(d.lock);
				const auto                  it = d.sections.find(SectionKey{ sx, sy, sz });
				if (it != d.sections.end()) {
					std::memcpy(bits, it->second.first.bits, sizeof(bits));
				}
			}
			const int          bit = dug_detail::bit_of(cell[0], cell[1], cell[2]);
			const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bit & 7));
			bits[bit >> 3] = static_cast<std::uint8_t>(dig ? bits[bit >> 3] | mask : bits[bit >> 3] & ~mask);
			std::uint32_t count = 0;
			for (const std::uint8_t byte : bits) {
				count += static_cast<std::uint32_t>(std::bitset<8>(byte).count());
			}
			set_dug(sx, sy, sz, count, count > 0 ? bits : nullptr);
			log_info("hc_debug_dig_cell: cell %d %d %d %s", cell[0], cell[1], cell[2], dig ? "dug" : "filled");
		}

		/// hc_debug_dig_cell: the cell of the wall under the crosshair, the one its face there belongs to
		/// @return false when there's no wall within reach
		bool crosshair_cell(int cell[3])
		{
			// the world's brushes alone: a player clip in front of a wall would stop the ray before it
			CTraceFilterWorldOnly filter;
			trace_t               tr;
			UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + MainViewForward() * CROSSHAIR_REACH, MASK_SOLID_BRUSHONLY, &filter, &tr);
			if (tr.fraction == 1.0f) {
				return false;
			}
			const Vector inside = tr.endpos - tr.plane.normal * INTO_WALL;
			const McVec  mc = source_to_mc(inside.Base(), current_slot());
			cell[0] = static_cast<int>(std::floor(mc.x));
			cell[1] = static_cast<int>(std::floor(mc.y));
			cell[2] = static_cast<int>(std::floor(mc.z));
			return true;
		}

		/// the sections dug_since gives, into m.sections
		/// @return how many it copied
		int sections_since(DigMask& m, std::uint32_t since, std::uint32_t* now)
		{
			const int changed = dug_since(since, nullptr, 0, now);
			if (changed <= 0) {
				return 0;
			}
			m.sections.resize(static_cast<std::size_t>(changed));
			return std::min(changed, dug_since(since, m.sections.data(), changed, now));
		}

		/// the mask's image laid out afresh in its layout, with every section that has cells dug, and the whole texture
		/// uploaded (made the first time: only ever once, at one size, since the material system's thread may draw with it)
		void rebuild_mask(DigMask& m)
		{
			m.rgba.assign(static_cast<std::size_t>(DigMaskLayout::TEXTURE) * DigMaskLayout::TEXTURE * TEXEL_BYTES, 0);
			m.dug = false;
			const int all = sections_since(m, 0, nullptr);
			for (int i = 0; i < all; ++i) {
				const DugSection& section = m.sections[i];
				const bool        dug = section.count > 0;
				if (write_section(m.layout, section.sx, section.sy, section.sz, dug ? section.bits : nullptr, m.rgba.data()) && dug) {
					m.dug = true;
				}
			}
			m.texture.set(m.rgba.data(), DigMaskLayout::TEXTURE, DigMaskLayout::TEXTURE);
			m.built = true;
		}

		/// the mask placed around the player: a new layout, and the image laid out afresh unless it's all zeros and stays so
		/// (no hole near the player before or after: the same zeros, nothing to clear or upload)
		void recentre_mask(DigMask& m, const McVec& player)
		{
			m.layout = DigMaskLayout::around(player.x, player.y, player.z);
			const bool rebuild = mask_needs_rebuild(m.built, m.dug, any_dug_in(m.layout));
			if (hc_debug_dig_mask.GetBool()) {
				log_info("dig: mask around %d %d %d (blocks from %d %d %d): %s", m.layout.ox + DigMaskLayout::CENTRE_XZ, m.layout.oy + DigMaskLayout::CENTRE_Y,
					m.layout.oz + DigMaskLayout::CENTRE_XZ, m.layout.ox, m.layout.oy, m.layout.oz,
					rebuild ? "laid out and uploaded again" : "no cell dug in it before or now, the layout alone moves");
			}
			if (rebuild) {
				rebuild_mask(m);
			}
		}

		/// one changed section into the mask and its texture: its 16 layers, a square of 16 x 16 texels each
		void update_mask(DigMask& m, const DugSection& section)
		{
			if (!write_section(m.layout, section.sx, section.sy, section.sz, section.count > 0 ? section.bits : nullptr, m.rgba.data())) {
				return;  // outside the mask: not cut out until the player comes near
			}
			if (section.count > 0) {
				m.dug = true;  // a section emptied leaves it as it is: another may still have cells dug in the image
			}
			// update_region takes the square's own rows, not the image's
			std::uint8_t block[SECTION_BLOCKS * SECTION_BLOCKS * TEXEL_BYTES];
			for (int y = 0; y < SECTION_BLOCKS; ++y) {
				int u = 0, v = 0;
				m.layout.texel(section.sx * SECTION_BLOCKS, section.sy * SECTION_BLOCKS + y, section.sz * SECTION_BLOCKS, u, v);
				for (int row = 0; row < SECTION_BLOCKS; ++row) {
					const std::uint8_t* from = m.rgba.data() + (static_cast<std::size_t>(v + row) * DigMaskLayout::TEXTURE + u) * TEXEL_BYTES;
					std::memcpy(block + row * SECTION_BLOCKS * TEXEL_BYTES, from, SECTION_BLOCKS * TEXEL_BYTES);
				}
				m.texture.update_region(u, v, SECTION_BLOCKS, SECTION_BLOCKS, block);
			}
		}

		/// the mask follows the store; without the shader dll it's all zeros
		void mask_update(const McVec& player)
		{
			auto& m = mask();
			if (!dig_shader_ready()) {
				m.state = {};
				return;
			}
			std::uint32_t now = m.version;
			const int     changed = sections_since(m, m.version, &now);
			m.version = now;
			if (!m.built || m.layout.needs_recentre(player.x, player.y, player.z)) {
				recentre_mask(m, player);  // the sections that changed are in it, or nothing dug is
			} else {
				for (int i = 0; i < changed; ++i) {
					update_mask(m, m.sections[i]);
				}
			}
			m.state.texture = m.texture.texture();
			m.layout.shader_constants(current_slot(), m.state.origin, m.state.layout);
			m.state.layout[3] = 1.0f;  // on: the cells dug stay cut out whatever hc_dig says
		}
	}

	void dig_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes)
	{
		if (type == proto::kRenDug) {
			if (bytes < sizeof(proto::RenDug)) {
				return;
			}
			proto::RenDug header;
			std::memcpy(&header, payload, sizeof(header));
			const bool has_bits = header.count > 0 && sizeof(header) + sizeof(DugSection::bits) <= bytes;
			set_dug(header.sx, header.sy, header.sz, has_bits ? header.count : 0, has_bits ? payload + sizeof(header) : nullptr);
		} else if (type == proto::kRenClearAll) {
			empty_sections([](const SectionKey&) { return true; });
		}
	}

	void dig_set_slot(MapSlot slot)
	{
		{
			auto&                       d = store();
			std::lock_guard<std::mutex> lock(d.lock);
			if (slot == d.slot) {
				return;
			}
			d.slot = slot;
		}
		// another map: what minecraft dug around the old one isn't anywhere near this one
		const float centre = static_cast<float>(slot.x_blocks());
		empty_sections([centre](const SectionKey& key) { return std::fabs(static_cast<float>(std::get<0>(key) * SECTION_BLOCKS) - centre) > KEEP_SLOT_BLOCKS; });
	}

	bool dig_shader_ready()
	{
		static std::atomic<int> logged{ -1 };  // what the log said last: 1 loaded, 0 not, -1 nothing yet
		const bool              ready = module_loaded(HC_SHADER_DLL);
		if (logged.exchange(ready ? 1 : 0) != (ready ? 1 : 0)) {
			if (ready) {
				log_info("dig: %s is loaded", HC_SHADER_DLL);
			} else {
				log_info("dig: no %s in this engine: digging is off", HC_SHADER_DLL);
			}
		}
		return ready;
	}

	bool dig_enabled()
	{
		const bool ready = dig_shader_ready();  // first: its line is logged whatever hc_dig says
		return ready && hc_dig.GetBool();
	}

	void dig_update(const McVec& player, bool in_game)
	{
		mask_update(player);
		// hc_dig 0 leaves the holes cut out (it only stops new ones): they're seen through all the same. the ones
		// outside the mask are drawn closed: nothing to see through there
		g_holes_near = in_game && dig_shader_ready() && any_dug_in(mask().layout);
		force_vis(g_holes_near);
	}

	bool dig_holes_near()
	{
		return g_holes_near;
	}

	void dig_shutdown()
	{
		force_vis(false);
		g_holes_near = false;
		auto& m = mask();
		m.state = {};  // first: HalfCraft_DigMask mustn't hand the shader dll the texture released next
		m.texture.release();
		m.built = false;
		auto&                       d = store();
		std::lock_guard<std::mutex> lock(d.lock);
		d.sections.clear();
	}

	const DigMaskState* dig_mask_state()
	{
		return &mask().state;
	}

	int dug_since(std::uint32_t since, DugSection* out, int max, std::uint32_t* now)
	{
		if (!dig_shader_ready()) {
			return 0;  // half-life draws its walls whole: they stay solid for minecraft too
		}
		auto&                       d = store();
		std::lock_guard<std::mutex> lock(d.lock);
		int                         changed = 0;
		for (const auto& entry : d.sections) {
			if (entry.second.second > since) {
				if (out && changed < max) {
					out[changed] = entry.second.first;
				}
				++changed;
			}
		}
		if (now) {
			*now = d.version;
		}
		return changed;
	}

	bool box_dug(const Vector& mins, const Vector& maxs)
	{
		if (!dig_shader_ready()) {
			return false;  // half-life draws its walls whole: they're there
		}
		auto&                       d = store();
		std::lock_guard<std::mutex> lock(d.lock);
		return source_box_dug(mins.Base(), maxs.Base(), d.slot, [&d](int x, int y, int z) { return cell_dug(d, x, y, z); });
	}
}

// server.dll's way to the cells minecraft dug (hc_bridge.h)
extern "C" __declspec(dllexport) int HalfCraft_DugSince(std::uint32_t since, halfcraft::DugSection* out, int max, std::uint32_t* now)
{
	return halfcraft::dug_since(since, out, max, now);
}

// the shader dll's way to the dig mask (hc_dig_abi.h, DigMaskFn)
extern "C" __declspec(dllexport) const halfcraft::DigMaskState* HalfCraft_DigMask()
{
	return halfcraft::dig_mask_state();
}

CON_COMMAND(hc_debug_dig_cell,
	"halfcraft: dig a cell out of half-life's walls without minecraft, or fill it again: hc_debug_dig_cell [<x> <y> <z> [1 dig | 0 fill]] (minecraft blocks; "
	"without them, the wall under the crosshair)")
{
	int  cell[3] = {};
	bool dig = true;
	if (args.ArgC() >= 4) {
		for (int k = 0; k < 3; ++k) {
			cell[k] = atoi(args[k + 1]);
		}
		dig = args.ArgC() < 5 || atoi(args[4]) != 0;
	} else if (!halfcraft::crosshair_cell(cell)) {
		halfcraft::log_info("hc_debug_dig_cell: no wall under the crosshair");
		return;
	}
	halfcraft::debug_dig_cell(cell, dig);
}
