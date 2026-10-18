// client.dll: the cells minecraft dug out of half-life's walls (see hc_dig.h).

#include "cbase.h"
#include "engine/IEngineTrace.h"
#include "view.h"

#include "tier0/valve_minmax_off.h"
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>

#include "halfcraft_protocol.h"
#include "client/hc_dig.h"
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
		constexpr float CROSSHAIR_REACH = 180.0f;  // units hc_debug_dig_cell looks for a wall in (4.5 blocks: minecraft's reach)
		constexpr float INTO_WALL = 0.5f;          // units behind the face under the crosshair: in the cell the face belongs to

		ConVar hc_dig("hc_dig", "1", FCVAR_ARCHIVE, "halfcraft: 0 - minecraft digs no new cells out of half-life's walls (the ones dug stay dug)");

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
			MapSlot slot;
			{
				auto&                       d = store();
				std::lock_guard<std::mutex> lock(d.lock);
				slot = d.slot;
			}
			const Vector inside = tr.endpos - tr.plane.normal * INTO_WALL;
			const McVec  mc = source_to_mc(inside.Base(), slot);
			cell[0] = static_cast<int>(std::floor(mc.x));
			cell[1] = static_cast<int>(std::floor(mc.y));
			cell[2] = static_cast<int>(std::floor(mc.z));
			return true;
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

	void dig_shutdown()
	{
		auto&                       d = store();
		std::lock_guard<std::mutex> lock(d.lock);
		d.sections.clear();
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
