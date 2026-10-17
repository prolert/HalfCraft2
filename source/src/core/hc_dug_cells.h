#pragma once

// the cells minecraft dug out of half-life's walls, as minecraft sends them (proto::kRenDug): a 512-byte mask for each
// 16x16x16 section, bit x + 16z + 256y of it. server.dll keeps them here (server/hc_dig.h) to cut them out of the
// collision it streams. pure code, no engine and no link.

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "hc_units.h"

namespace halfcraft
{
	namespace dug_detail
	{
		inline constexpr int SECTION_BLOCKS = 16;
		inline constexpr int SECTION_BYTES = SECTION_BLOCKS * SECTION_BLOCKS * SECTION_BLOCKS / 8;
		/// a cell coordinate further out than this is clamped to it: far beyond every map's slot, and an int still
		inline constexpr double FAR_BLOCKS = 1 << 28;

		inline int section_of(int block)
		{
			return block >= 0 ? block / SECTION_BLOCKS : -((-block + SECTION_BLOCKS - 1) / SECTION_BLOCKS);
		}

		/// v within +-FAR_BLOCKS (not a number: -FAR_BLOCKS)
		inline double clamp_far(double v)
		{
			return v > -FAR_BLOCKS ? (v < FAR_BLOCKS ? v : FAR_BLOCKS) : -FAR_BLOCKS;
		}

		/// the cell a coordinate lies in
		inline int cell_floor(double v)
		{
			return static_cast<int>(std::floor(clamp_far(v)));
		}

		/// the last cell a box ending at v reaches into (one ending on a cell's side doesn't reach into it)
		inline int cell_before(double v)
		{
			return static_cast<int>(std::ceil(clamp_far(v))) - 1;
		}

		inline int bit_of(int x, int y, int z)
		{
			return (x & 15) + SECTION_BLOCKS * (z & 15) + SECTION_BLOCKS * SECTION_BLOCKS * (y & 15);
		}
	}

	class DugCells
	{
	public:
		using Cell = std::array<int, 3>;

		/// a section's mask, from minecraft
		/// @param sx, sy, sz - the section (minecraft section coords: its blocks start at 16 times them)
		/// @param bits - its 512 bytes, bit x + 16z + 256y; nullptr: nothing dug in it
		/// @param changed - the cells dug or filled since its last mask, appended
		void set(int sx, int sy, int sz, const std::uint8_t* bits, std::vector<Cell>& changed);
		void clear();
		/// @param x, y, z - a minecraft block
		bool dug(int x, int y, int z) const;
		/// how many cells are dug
		std::size_t count() const { return count_; }
		/// the dug cells a box reaches into (cell x when x + 1 > lo and x < hi, each axis: one it only touches isn't)
		/// @param out - replaced by them
		void cells_in(const float lo[3], const float hi[3], std::vector<Cell>& out) const;
		/// the dug cells whose centre lies within radius of p, nearest first
		/// @param max - at most this many of them
		/// @param out - replaced by them
		void nearest(const McVec& p, double radius, std::size_t max, std::vector<Cell>& out) const;

	private:
		using Bits = std::array<std::uint8_t, dug_detail::SECTION_BYTES>;

		/// calls visit(section, bits) for each section with a cell dug that lies in the cells [from, to] (inclusive),
		/// looking up the sections there or, when the box spans more of them than there are, going over the ones there are
		template <class Visit>
		void each_section(const int from[3], const int to[3], Visit&& visit) const;
		/// the dug cells of a section within the cells [from, to], appended to out
		static void cells_of(const Cell& section, const Bits& bits, const int from[3], const int to[3], std::vector<Cell>& out);

		std::map<Cell, Bits> sections_;  // only the ones with a cell dug
		std::size_t          count_ = 0;
	};

	inline void DugCells::set(int sx, int sy, int sz, const std::uint8_t* bits, std::vector<Cell>& changed)
	{
		using namespace dug_detail;
		const Cell section{ sx, sy, sz };
		const auto it = sections_.find(section);
		bool       any = false;
		for (int i = 0; i < SECTION_BYTES; ++i) {
			const std::uint8_t was = it != sections_.end() ? it->second[static_cast<std::size_t>(i)] : std::uint8_t{ 0 };
			const std::uint8_t now = bits ? bits[i] : std::uint8_t{ 0 };
			any = any || now != 0;
			count_ = count_ + std::bitset<8>(now).count() - std::bitset<8>(was).count();
			for (int b = 0; b < 8; ++b) {
				if (((was ^ now) >> b) & 1) {
					const int bit = i * 8 + b;
					changed.push_back({ sx * SECTION_BLOCKS + (bit & 15), sy * SECTION_BLOCKS + (bit >> 8), sz * SECTION_BLOCKS + ((bit >> 4) & 15) });
				}
			}
		}
		if (!any) {
			if (it != sections_.end()) {
				sections_.erase(it);
			}
			return;
		}
		Bits& kept = it != sections_.end() ? it->second : sections_[section];
		std::copy(bits, bits + SECTION_BYTES, kept.begin());
	}

	inline void DugCells::clear()
	{
		sections_.clear();
		count_ = 0;
	}

	inline bool DugCells::dug(int x, int y, int z) const
	{
		using namespace dug_detail;
		const auto it = sections_.find({ section_of(x), section_of(y), section_of(z) });
		if (it == sections_.end()) {
			return false;
		}
		const int bit = bit_of(x, y, z);
		return ((it->second[static_cast<std::size_t>(bit >> 3)] >> (bit & 7)) & 1) != 0;
	}

	template <class Visit>
	void DugCells::each_section(const int from[3], const int to[3], Visit&& visit) const
	{
		using namespace dug_detail;
		int    lo[3], hi[3];
		double spanned = 1.0;
		for (int k = 0; k < 3; ++k) {
			if (to[k] < from[k]) {
				return;
			}
			lo[k] = section_of(from[k]);
			hi[k] = section_of(to[k]);
			spanned *= hi[k] - lo[k] + 1.0;
		}
		if (spanned > static_cast<double>(sections_.size())) {
			for (const auto& entry : sections_) {
				const Cell& s = entry.first;
				if (s[0] >= lo[0] && s[0] <= hi[0] && s[1] >= lo[1] && s[1] <= hi[1] && s[2] >= lo[2] && s[2] <= hi[2]) {
					visit(s, entry.second);
				}
			}
			return;
		}
		for (int sx = lo[0]; sx <= hi[0]; ++sx) {
			for (int sy = lo[1]; sy <= hi[1]; ++sy) {
				for (int sz = lo[2]; sz <= hi[2]; ++sz) {
					const auto it = sections_.find({ sx, sy, sz });
					if (it != sections_.end()) {
						visit(it->first, it->second);
					}
				}
			}
		}
	}

	inline void DugCells::cells_of(const Cell& section, const Bits& bits, const int from[3], const int to[3], std::vector<Cell>& out)
	{
		using namespace dug_detail;
		for (int i = 0; i < SECTION_BYTES; ++i) {
			const std::uint8_t byte = bits[static_cast<std::size_t>(i)];
			for (int b = 0; byte != 0 && b < 8; ++b) {
				if (((byte >> b) & 1) == 0) {
					continue;
				}
				const int  bit = i * 8 + b;
				const Cell cell{ section[0] * SECTION_BLOCKS + (bit & 15), section[1] * SECTION_BLOCKS + (bit >> 8), section[2] * SECTION_BLOCKS + ((bit >> 4) & 15) };
				if (cell[0] >= from[0] && cell[0] <= to[0] && cell[1] >= from[1] && cell[1] <= to[1] && cell[2] >= from[2] && cell[2] <= to[2]) {
					out.push_back(cell);
				}
			}
		}
	}

	inline void DugCells::cells_in(const float lo[3], const float hi[3], std::vector<Cell>& out) const
	{
		using namespace dug_detail;
		out.clear();
		const int from[3] = { cell_floor(lo[0]), cell_floor(lo[1]), cell_floor(lo[2]) };
		const int to[3] = { cell_before(hi[0]), cell_before(hi[1]), cell_before(hi[2]) };
		each_section(from, to, [&](const Cell& section, const Bits& bits) { cells_of(section, bits, from, to, out); });
	}

	inline void DugCells::nearest(const McVec& p, double radius, std::size_t max, std::vector<Cell>& out) const
	{
		using namespace dug_detail;
		out.clear();
		// the cells whose centre can lie within radius: those the box of the sphere reaches into
		const int         from[3] = { cell_floor(p.x - radius), cell_floor(p.y - radius), cell_floor(p.z - radius) };
		const int         to[3] = { cell_before(p.x + radius), cell_before(p.y + radius), cell_before(p.z + radius) };
		std::vector<Cell> around;
		each_section(from, to, [&](const Cell& section, const Bits& bits) { cells_of(section, bits, from, to, around); });
		std::vector<std::pair<double, Cell>> by_distance;
		by_distance.reserve(around.size());
		for (const Cell& cell : around) {
			const double dx = cell[0] + 0.5 - p.x, dy = cell[1] + 0.5 - p.y, dz = cell[2] + 0.5 - p.z;
			const double d2 = dx * dx + dy * dy + dz * dz;
			if (d2 <= radius * radius) {
				by_distance.emplace_back(d2, cell);
			}
		}
		std::sort(by_distance.begin(), by_distance.end());
		for (std::size_t i = 0; i < by_distance.size() && i < max; ++i) {
			out.push_back(by_distance[i].second);
		}
	}

	/// whether every cell a source box reaches into with some of its volume (not one it only touches) is dug
	/// @param mins, maxs - the box, source units
	/// @param slot - where the map sits in minecraft
	/// @param dug - bool(int x, int y, int z): whether that cell is
	/// @return false for a box with no volume
	template <class Dug>
	bool source_box_dug(const float mins[3], const float maxs[3], MapSlot slot, Dug&& dug)
	{
		using namespace dug_detail;
		// minecraft z is -source y: the corners swap there
		const McVec a = source_to_mc(mins, slot), b = source_to_mc(maxs, slot);
		const int   from[3] = { cell_floor(std::min(a.x, b.x)), cell_floor(std::min(a.y, b.y)), cell_floor(std::min(a.z, b.z)) };
		const int   to[3] = { cell_before(std::max(a.x, b.x)), cell_before(std::max(a.y, b.y)), cell_before(std::max(a.z, b.z)) };
		if (to[0] < from[0] || to[1] < from[1] || to[2] < from[2]) {
			return false;
		}
		for (int x = from[0]; x <= to[0]; ++x) {
			for (int y = from[1]; y <= to[1]; ++y) {
				for (int z = from[2]; z <= to[2]; ++z) {
					if (!dug(x, y, z)) {
						return false;
					}
				}
			}
		}
		return true;
	}
}
