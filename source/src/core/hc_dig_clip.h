#pragma once

// the cells dug out of a wall, cut out of the world's brushes: neighbouring cells merge into boxes (merge_cells), and a
// brush's convex minus the boxes is convexes again (subtract_boxes), so the collision server.dll streams to minecraft
// has the holes that were dug. pure code, no engine and no link.
//
// ported from SkyCraft's Clip.h (skse/src/Clip.h, MIT, chasmlol): merge_cells is its Clip::Merge, subtract_boxes its
// Clip::Subtract's cut done on convex solids instead of polygons.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hc_collision_shapes.h"

namespace halfcraft
{
	/// a box of dug cells in minecraft blocks (whole cells: its corners are whole numbers)
	struct DigBox
	{
		float lo[3], hi[3];
	};

	/// a slice of a brush thinner than this (blocks, a 1/1024th) is no piece: it holds no voxel and its triangles are
	/// degenerate. two planes with a normal and a distance this close are one plane.
	inline constexpr float DIG_SLIVER = 1.0f / 1024;

	namespace dig_detail
	{
		using Cell = std::array<int, 3>;
		using Plane = std::array<float, 4>;

		/// 21 bits an axis: the cells of a map lie in its slot of the minecraft world (hc_units.h), whose x stays under 2^21 blocks
		inline std::uint64_t cell_key(int x, int y, int z)
		{
			const auto bits = [](int v) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(v) & 0x1FFFFFu); };
			return (bits(x) << 42) | (bits(y) << 21) | bits(z);
		}

		/// the plane along an axis through value: inside below it (upper) or above it. n.p + d <= 0 inside.
		inline Plane axis_plane(std::size_t axis, bool upper, float value)
		{
			Plane plane = { 0.0f, 0.0f, 0.0f, upper ? -value : value };
			plane[axis] = upper ? 1.0f : -1.0f;
			return plane;
		}

		/// the box overlaps the convex's bounds, more than touches them
		inline bool overlaps(const ColPrimitives::Convex& cvx, const DigBox& box)
		{
			for (int k = 0; k < 3; ++k) {
				if (box.lo[k] >= cvx.hi[k] || box.hi[k] <= cvx.lo[k]) {
					return false;
				}
			}
			return true;
		}

		/// one layer of a piece cut by a box, as a piece of its own appended to out: the piece's planes with their flags,
		/// then the box's planes that bound the layer (the slab's, and the edge it lies against) that the piece doesn't have,
		/// flagged cut_flags. a layer thinner than DIG_SLIVER (in the bounds [lo, hi] it's given) isn't a piece at all.
		inline void add_layer(const ColPrimitives::Convex& piece, const float lo[3], const float hi[3], const std::vector<Plane>& slab, const Plane& edge, std::uint32_t cut_flags,
			std::vector<ColPrimitives::Convex>& out)
		{
			for (int k = 0; k < 3; ++k) {
				if (hi[k] - lo[k] < DIG_SLIVER) {
					return;
				}
			}
			ColPrimitives::Convex layer;
			layer.flags = piece.flags;
			for (int k = 0; k < 3; ++k) {
				layer.lo[k] = lo[k];
				layer.hi[k] = hi[k];
			}
			// one at a time: a range insert's strong guarantee needs exception handling, which the sdk builds without (c4530)
			for (std::size_t i = 0; i < piece.planes.size(); ++i) {
				layer.planes.push_back(piece.planes[i]);
				layer.plane_flags.push_back(i < piece.plane_flags.size() ? piece.plane_flags[i] : 0u);
			}
			const auto add = [&layer, cut_flags](const Plane& plane) {
				for (const Plane& have : layer.planes) {
					if (have[0] == plane[0] && have[1] == plane[1] && have[2] == plane[2] && std::fabs(have[3] - plane[3]) < DIG_SLIVER) {
						return;
					}
				}
				layer.planes.push_back(plane);
				layer.plane_flags.push_back(cut_flags);
			};
			for (const Plane& plane : slab) {
				add(plane);
			}
			add(edge);
			out.push_back(std::move(layer));
		}

		/// a piece minus a box it overlaps, as layers appended to out: below the box on x, above it on x, then (in the
		/// box's slab on x) below and above it on y, then (in its slab on x and y) on z. what's left is inside the box.
		inline void cut_piece(const ColPrimitives::Convex& piece, const DigBox& box, std::uint32_t cut_flags, std::vector<ColPrimitives::Convex>& out)
		{
			float              rest_lo[3] = { piece.lo[0], piece.lo[1], piece.lo[2] }, rest_hi[3] = { piece.hi[0], piece.hi[1], piece.hi[2] };
			std::vector<Plane> slab;  // the box's planes on the axes done: the layers still to come lie between them
			for (std::size_t axis = 0; axis < 3; ++axis) {
				if (box.lo[axis] > rest_lo[axis]) {
					float hi[3] = { rest_hi[0], rest_hi[1], rest_hi[2] };
					hi[axis] = std::min(rest_hi[axis], box.lo[axis]);
					add_layer(piece, rest_lo, hi, slab, axis_plane(axis, true, box.lo[axis]), cut_flags, out);
				}
				if (box.hi[axis] < rest_hi[axis]) {
					float lo[3] = { rest_lo[0], rest_lo[1], rest_lo[2] };
					lo[axis] = std::max(rest_lo[axis], box.hi[axis]);
					add_layer(piece, lo, rest_hi, slab, axis_plane(axis, false, box.hi[axis]), cut_flags, out);
				}
				rest_lo[axis] = std::max(rest_lo[axis], box.lo[axis]);
				rest_hi[axis] = std::min(rest_hi[axis], box.hi[axis]);
				slab.push_back(axis_plane(axis, false, box.lo[axis]));
				slab.push_back(axis_plane(axis, true, box.hi[axis]));
			}
		}
	}

	/// dug cells -> boxes: neighbours merge, so a crater is a few boxes (a few cuts, a few pieces) and not a cut for every
	/// cell. greedy: from each cell left, in y, z, x order, as far as it goes along x, then z (whole rows), then y (whole
	/// layers). a box runs exactly from its first cell to its last one's far corner.
	/// @param cells - the cells' minimum corners (minecraft blocks); one that's there twice counts once
	/// @param out - replaced by the boxes
	inline void merge_cells(std::vector<std::array<int, 3>> cells, std::vector<DigBox>& out)
	{
		using dig_detail::Cell;
		using dig_detail::cell_key;
		out.clear();
		std::unordered_set<std::uint64_t> left;
		left.reserve(cells.size() * 2);
		for (const Cell& c : cells) {
			left.insert(cell_key(c[0], c[1], c[2]));
		}
		std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return std::tie(a[1], a[2], a[0]) < std::tie(b[1], b[2], b[0]); });
		const auto has = [&left](int x, int y, int z) { return left.count(cell_key(x, y, z)) != 0; };
		for (const Cell& c : cells) {
			if (!has(c[0], c[1], c[2])) {
				continue;
			}
			int x1 = c[0], z1 = c[2], y1 = c[1];
			while (has(x1 + 1, c[1], c[2])) {
				++x1;
			}
			for (bool grow = true; grow;) {
				for (int x = c[0]; x <= x1 && grow; ++x) {
					grow = has(x, c[1], z1 + 1);
				}
				z1 += grow ? 1 : 0;
			}
			for (bool grow = true; grow;) {
				for (int z = c[2]; z <= z1 && grow; ++z) {
					for (int x = c[0]; x <= x1 && grow; ++x) {
						grow = has(x, y1 + 1, z);
					}
				}
				y1 += grow ? 1 : 0;
			}
			for (int y = c[1]; y <= y1; ++y) {
				for (int z = c[2]; z <= z1; ++z) {
					for (int x = c[0]; x <= x1; ++x) {
						left.erase(cell_key(x, y, z));
					}
				}
			}
			out.push_back({ { static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]) },
				{ static_cast<float>(x1 + 1), static_cast<float>(y1 + 1), static_cast<float>(z1 + 1) } });
		}
	}

	/// a brush's convex minus dug boxes: the convexes outside every box, appended to out. each box in turn cuts every
	/// piece there is (the brush first): a piece whose bounds it only touches, or misses, stays as it is; otherwise the
	/// piece becomes the layers outside the box (see dig_detail::cut_piece) and the part inside the box is gone.
	/// a layer has its piece's planes with their flags (0 when the piece has no plane_flags) and the box's planes it
	/// didn't have already, flagged cut_flags: those are the faces of the hole's wall. a plane the piece has (the same
	/// normal, a distance within DIG_SLIVER) isn't added again, and a layer thinner than DIG_SLIVER is dropped.
	/// @param cut_flags - the flags of the faces the cut makes (core/hc_dig_material.h's dig_flags of the material)
	inline void subtract_boxes(const ColPrimitives::Convex& in, const std::vector<DigBox>& boxes, std::uint32_t cut_flags, std::vector<ColPrimitives::Convex>& out)
	{
		std::vector<ColPrimitives::Convex> pieces, next;
		pieces.push_back(in);
		for (const DigBox& box : boxes) {
			next.clear();
			for (auto& piece : pieces) {
				if (dig_detail::overlaps(piece, box)) {
					dig_detail::cut_piece(piece, box, cut_flags, next);
				} else {
					next.push_back(std::move(piece));
				}
			}
			pieces.swap(next);
		}
		for (auto& piece : pieces) {
			out.push_back(std::move(piece));
		}
	}
}
