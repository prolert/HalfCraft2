// core_tests: plain checks over the pure c++ core (source/src/core's headers: no sdk, no engine, no link), the code
// server.dll and client.dll share with no engine under it. built and run by tools/debug/core_tests.py (make core-test).
//
// CHECK(condition) counts a check and, when it fails, says which one and goes on: one run shows everything wrong.
// each test is a function named for what it holds; run_tests calls them all and returns the exit code.

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

#include "core/hc_bsp_tree.h"
#include "core/hc_collision_shapes.h"
#include "core/hc_dig_material.h"
#include "core/hc_dig_clip.h"
#include "core/hc_dig_mask.h"
#include "core/hc_dig_void.h"
#include "core/hc_dug_cells.h"

using namespace halfcraft;

namespace
{
	int checks = 0, failed = 0;

	bool record(bool ok, const char* expression, const char* file, int line)
	{
		++checks;
		if (!ok) {
			++failed;
			std::printf("FAILED %s:%d: %s\n", file, line, expression);
		}
		return ok;
	}
}

#define CHECK(condition) record(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

namespace
{
	using Cvx = ColPrimitives::Convex;
	using Poly = std::vector<std::array<float, 3>>;

	constexpr std::uint32_t D = PRIM_DIGGABLE;

	/// the dig material k in a primitive's flags
	constexpr std::uint32_t M(std::uint32_t k)
	{
		return k << PRIM_MATERIAL_SHIFT;
	}

	bool near(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	/// the cube [lo, hi]^3 as a convex: its planes are +x, -x, +y, -y, +z, -z
	Cvx cube(float lo, float hi)
	{
		Cvx cvx;
		cvx.planes.push_back({ 1, 0, 0, -hi });
		cvx.planes.push_back({ -1, 0, 0, lo });
		cvx.planes.push_back({ 0, 1, 0, -hi });
		cvx.planes.push_back({ 0, -1, 0, lo });
		cvx.planes.push_back({ 0, 0, 1, -hi });
		cvx.planes.push_back({ 0, 0, -1, lo });
		for (int k = 0; k < 3; ++k) {
			cvx.lo[k] = lo;
			cvx.hi[k] = hi;
		}
		return cvx;
	}

	/// a triangle's normal, as long as twice its area
	std::array<float, 3> triangle_normal(const float* v)
	{
		const float e1[3] = { v[3] - v[0], v[4] - v[1], v[5] - v[2] }, e2[3] = { v[6] - v[0], v[7] - v[1], v[8] - v[2] };
		return { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	}

	/// a convex polygon's normal, as long as twice its area: its fan from the first corner, summed
	std::array<float, 3> polygon_normal(const Poly& poly)
	{
		std::array<float, 3> sum = { 0, 0, 0 };
		for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
			const float tri[9] = { poly[0][0], poly[0][1], poly[0][2], poly[i][0], poly[i][1], poly[i][2], poly[i + 1][0], poly[i + 1][1], poly[i + 1][2] };
			const auto  n = triangle_normal(tri);
			for (int k = 0; k < 3; ++k) {
				sum[k] += n[k];
			}
		}
		return sum;
	}

	float length(const std::array<float, 3>& v)
	{
		return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	/// which of a cube's faces (the order of cube()'s planes) a normal looks out of: the axis it's longest along, then its sign
	std::size_t cube_face_of(const std::array<float, 3>& n)
	{
		std::size_t axis = 0;
		for (std::size_t k = 1; k < 3; ++k) {
			if (std::fabs(n[k]) > std::fabs(n[axis])) {
				axis = k;
			}
		}
		return axis * 2 + (n[axis] < 0.0f ? 1 : 0);
	}

	using Cell = std::array<int, 3>;

	bool is_box(const DigBox& box, float x0, float y0, float z0, float x1, float y1, float z1)
	{
		return box.lo[0] == x0 && box.lo[1] == y0 && box.lo[2] == z0 && box.hi[0] == x1 && box.hi[1] == y1 && box.hi[2] == z1;
	}

	bool same_convex(const Cvx& a, const Cvx& b)
	{
		return a.planes == b.planes && std::equal(a.lo, a.lo + 3, b.lo) && std::equal(a.hi, a.hi + 3, b.hi) && a.flags == b.flags && a.plane_flags == b.plane_flags;
	}

	/// the voxels of region (0, 0, 0) that convexes hold
	RegionVoxels voxels_of(const std::vector<Cvx>& convexes)
	{
		ColPrimitives prims;
		for (const Cvx& cvx : convexes) {
			prims.convexes.push_back(cvx);
		}
		RegionVoxels voxels;
		voxelize_region(prims, 0, 0, 0, voxels);
		return voxels;
	}

	int voxel_count(const RegionVoxels& voxels)
	{
		int count = 0;
		for (const std::uint64_t row : voxels) {
			count += static_cast<int>(std::bitset<64>(row).count());
		}
		return count;
	}

	/// a brush's voxels (region 0, 0, 0) without the blocks of cells, which lie in the region: a block is 8 voxels an edge
	RegionVoxels voxels_without(const Cvx& brush, const std::vector<Cell>& cells)
	{
		RegionVoxels voxels = voxels_of({ brush });
		for (const Cell& cell : cells) {
			for (int dy = 0; dy < 8; ++dy) {
				for (int dz = 0; dz < 8; ++dz) {
					voxels[(cell[1] * 8 + dy) * REGION_VOXELS + cell[2] * 8 + dz] &= ~(0xFFull << (cell[0] * 8));
				}
			}
		}
		return voxels;
	}

	/// the cells cut out of the brush (merge_cells, subtract_boxes) leave the brush's voxels less the cells' blocks
	bool cut_matches(const Cvx& brush, const std::vector<Cell>& cells)
	{
		std::vector<DigBox> boxes;
		merge_cells(cells, boxes);
		std::vector<Cvx> pieces;
		subtract_boxes(brush, boxes, 0, pieces);
		return voxels_of(pieces) == voxels_without(brush, cells);
	}

	/// how many of the brush's voxels the cells' blocks hold
	int voxels_in(const Cvx& brush, const std::vector<Cell>& cells)
	{
		return voxel_count(voxels_of({ brush })) - voxel_count(voxels_without(brush, cells));
	}

	/// how many of a piece's planes have those flags
	std::size_t planes_flagged(const Cvx& piece, std::uint32_t flags)
	{
		return static_cast<std::size_t>(std::count(piece.plane_flags.begin(), piece.plane_flags.end(), flags));
	}

	/// every face of a convex lies within its bounds: its planes make the box its bounds say
	bool fits_bounds(const Cvx& cvx)
	{
		Poly poly;
		for (std::size_t i = 0; i < cvx.planes.size(); ++i) {
			if (!convex_face(cvx, i, poly)) {
				continue;
			}
			for (const auto& corner : poly) {
				for (int k = 0; k < 3; ++k) {
					if (corner[k] < cvx.lo[k] - 1e-4f || corner[k] > cvx.hi[k] + 1e-4f) {
						return false;
					}
				}
			}
		}
		return true;
	}

	float bounds_volume(const Cvx& cvx)
	{
		return (cvx.hi[0] - cvx.lo[0]) * (cvx.hi[1] - cvx.lo[1]) * (cvx.hi[2] - cvx.lo[2]);
	}

	void test_convex_face_gives_a_cube_its_squares()
	{
		Cvx  cvx = cube(0, 1);
		Poly poly;
		CHECK(convex_face(cvx, 0, poly));
		CHECK(poly.size() == 4);
		for (const auto& corner : poly) {
			CHECK(near(corner[0], 1.0f, 1e-5f));
			CHECK(near(corner[1], 0.0f, 1e-5f) || near(corner[1], 1.0f, 1e-5f));
			CHECK(near(corner[2], 0.0f, 1e-5f) || near(corner[2], 1.0f, 1e-5f));
		}
		// counter-clockwise seen from outside: its normal points out of the cube, along +x
		const auto n = polygon_normal(poly);
		CHECK(near(length(n) / 2.0f, 1.0f));
		CHECK(n[0] > 0.0f);

		// a plane outside the others' box has no face, and neither has one with no normal
		cvx.planes.push_back({ 1, 0, 0, -2 });
		cvx.planes.push_back({ 0, 0, 0, 0 });
		CHECK(!convex_face(cvx, 6, poly));
		CHECK(!convex_face(cvx, 7, poly));
	}

	void test_face_flags_take_the_planes_material()
	{
		Cvx cvx = cube(0, 1);
		cvx.flags = D | M(1);
		CHECK(face_flags(cvx, 0) == cvx.flags);
		cvx.plane_flags = { D | M(6), 0, 0, 0, 0, 0 };
		CHECK(face_flags(cvx, 0) == (D | M(6)));
		CHECK(face_flags(cvx, 1) == cvx.flags);
		cvx.flags = PRIM_SKY;
		CHECK(face_flags(cvx, 0) == (PRIM_SKY | D | M(6)));
	}

	void test_triangulate_gives_each_face_its_flags()
	{
		ColPrimitives prims;
		prims.tris.push_back({ { 0, 0, 0, 1, 0, 0, 0, 0, 1 }, 1 });
		Cvx cvx = cube(0, 1);
		cvx.flags = PRIM_SKY;
		for (std::uint32_t i = 0; i < 6; ++i) {
			cvx.plane_flags.push_back(D | M(i + 1));
		}
		prims.convexes.push_back(cvx);
		std::vector<ColPrimitives::Tri> tris;
		triangulate(prims, tris);
		CHECK(tris.size() == 13);
		if (tris.size() != 13) {
			return;
		}
		CHECK(tris[0].flags == 1);
		int on_face[6] = {};
		for (std::size_t t = 1; t < tris.size(); ++t) {
			const std::size_t face = cube_face_of(triangle_normal(tris[t].v));
			++on_face[face];
			CHECK(tris[t].flags == (PRIM_SKY | D | M(static_cast<std::uint32_t>(face) + 1)));
		}
		for (const int count : on_face) {
			CHECK(count == 2);
		}
	}

	void test_dig_material_maps_surface_materials()
	{
		CHECK(dig_material('C') == DIG_STONE);
		CHECK(dig_material('T') == DIG_STONE_BRICKS);
		CHECK(dig_material('M') == DIG_IRON);
		CHECK(dig_material('G') == DIG_IRON);
		CHECK(dig_material('V') == DIG_IRON);
		CHECK(dig_material('P') == DIG_IRON);
		CHECK(dig_material('W') == DIG_PLANKS);
		CHECK(dig_material('U') == DIG_PLANKS);
		CHECK(dig_material('D') == DIG_DIRT);
		CHECK(dig_material('Y') == DIG_GLASS);
		CHECK(dig_material('F') == DIG_STONE);
		CHECK(dig_material('S') == DIG_STONE);
		CHECK(dig_material('N') == DIG_STONE);
		CHECK(dig_material('Z') == DIG_STONE);
		CHECK(dig_material('\0') == DIG_STONE);
		CHECK(dig_flags(DIG_STONE) == 0x104u);
		CHECK(dig_flags(DIG_GLASS) == 0x604u);
	}

	void test_brush_material()
	{
		CHECK(brush_material({}) == DIG_STONE);
		CHECK(brush_material({ DIG_NONE, DIG_NONE }) == DIG_STONE);
		CHECK(brush_material({ DIG_NONE, DIG_DIRT }) == DIG_DIRT);
		CHECK(brush_material({ DIG_PLANKS, DIG_NONE, DIG_STONE, DIG_PLANKS }) == DIG_PLANKS);
		CHECK(brush_material({ DIG_GLASS, DIG_IRON, DIG_IRON, DIG_GLASS }) == DIG_GLASS);  // a tie: the first
		CHECK(brush_material({ DIG_NONE, DIG_IRON, DIG_GLASS, DIG_GLASS, DIG_IRON }) == DIG_IRON);
	}

	void test_merge_cells_makes_boxes()
	{
		std::vector<DigBox> boxes;
		merge_cells({ { 0, 0, 0 }, { 1, 0, 0 } }, boxes);
		CHECK(boxes.size() == 1 && is_box(boxes[0], 0, 0, 0, 2, 1, 1));

		// an L: the row along x first, then what's left
		merge_cells({ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 } }, boxes);
		CHECK(boxes.size() == 2);
		if (boxes.size() == 2) {
			CHECK(is_box(boxes[0], 0, 0, 0, 2, 1, 1));
			CHECK(is_box(boxes[1], 0, 0, 1, 1, 1, 2));
		}

		// a cube of cells, one of them twice
		std::vector<Cell> cube_cells;
		for (int y = 0; y < 2; ++y) {
			for (int z = 0; z < 2; ++z) {
				for (int x = 0; x < 2; ++x) {
					cube_cells.push_back({ x, y, z });
				}
			}
		}
		cube_cells.push_back({ 1, 0, 1 });
		merge_cells(cube_cells, boxes);
		CHECK(boxes.size() == 1 && is_box(boxes[0], 0, 0, 0, 2, 2, 2));

		merge_cells({ { -3, 5, -1 } }, boxes);
		CHECK(boxes.size() == 1 && is_box(boxes[0], -3, 5, -1, -2, 6, 0));

		merge_cells({}, boxes);  // replaces what was there
		CHECK(boxes.empty());
	}

	void test_subtract_boxes_cuts_the_cells_voxels()
	{
		CHECK(cut_matches(cube(2, 4), { { 2, 2, 2 } }));
		CHECK(cut_matches(cube(2, 4), { { 3, 3, 3 } }));
		CHECK(cut_matches(cube(2, 4), { { 2, 2, 2 }, { 3, 2, 2 }, { 2, 2, 3 } }));
		CHECK(cut_matches(cube(2, 4), { { 4, 2, 2 } }));
		CHECK(cut_matches(cube(2.25f, 3.75f), { { 3, 3, 3 } }));
		CHECK(cut_matches(cube(2.25f, 3.75f), { { 2, 3, 2 } }));
		// and they cut something: a cell inside takes its voxels (8 an edge), one that only touches takes none
		CHECK(voxels_in(cube(2, 4), { { 2, 2, 2 } }) == 512);
		CHECK(voxels_in(cube(2, 4), { { 2, 2, 2 }, { 3, 2, 2 }, { 2, 2, 3 } }) == 3 * 512);
		CHECK(voxels_in(cube(2, 4), { { 4, 2, 2 } }) == 0);
		CHECK(voxels_in(cube(2.25f, 3.75f), { { 3, 3, 3 } }) == 6 * 6 * 6);
	}

	void test_subtract_boxes_flags_the_cut_faces()
	{
		const std::uint32_t stone = dig_flags(DIG_STONE), bricks = dig_flags(DIG_STONE_BRICKS), planks = dig_flags(DIG_PLANKS);
		Cvx                 brush = cube(2, 4);
		brush.flags = stone;
		brush.plane_flags.assign(6, bricks);
		std::vector<DigBox> boxes;
		merge_cells({ { 2, 2, 2 } }, boxes);
		std::vector<Cvx> pieces;
		subtract_boxes(brush, boxes, planks, pieces);

		// what the cube has left outside the cell: x > 3, then y > 3 at x 2..3, then z > 3 at x and y 2..3 (the layers
		// below the cell are nothing: the cube starts where the cell does)
		CHECK(pieces.size() == 3);
		if (pieces.size() != 3) {
			return;
		}
		const std::size_t plane_counts[3] = { 7, 8, 9 };
		ColPrimitives     prims;
		for (std::size_t k = 0; k < 3; ++k) {
			CHECK(pieces[k].planes.size() == plane_counts[k]);
			CHECK(pieces[k].flags == stone);
			CHECK(pieces[k].plane_flags.size() == pieces[k].planes.size());
			CHECK(planes_flagged(pieces[k], bricks) == 6);  // the cube's own faces keep theirs...
			CHECK(planes_flagged(pieces[k], planks) == plane_counts[k] - 6);  // ...and the cell's new ones are the cut's
			prims.convexes.push_back(pieces[k]);
		}

		// their faces, by axis, where they lie (2, 3 or 4) and which way they face: the cube's face at 2 faces -axis and
		// what's left of it is 3 square blocks, at 4 it's 4 facing +axis, and the cut at 3 is planks
		std::vector<ColPrimitives::Tri> tris;
		triangulate(prims, tris);
		float area[3][3][2] = {};
		for (const auto& tri : tris) {
			const auto  n = triangle_normal(tri.v);
			const float twice_area = length(n);
			if (twice_area < 1e-5f) {
				continue;  // a corner of a face twice: no triangle
			}
			std::size_t axis = 0;
			for (std::size_t k = 1; k < 3; ++k) {
				if (std::fabs(n[k]) > std::fabs(n[axis])) {
					axis = k;
				}
			}
			const int slot = static_cast<int>(std::lround(tri.v[axis])) - 2;
			if (!CHECK(slot >= 0 && slot <= 2 && near(tri.v[axis], static_cast<float>(slot + 2)))) {
				continue;
			}
			area[axis][slot][n[axis] > 0.0f ? 1 : 0] += twice_area / 2.0f;
			CHECK(tri.flags == (slot == 1 ? planks : bricks));
		}
		for (std::size_t axis = 0; axis < 3; ++axis) {
			CHECK(near(area[axis][0][0], 3.0f));
			CHECK(near(area[axis][0][1], 0.0f));
			CHECK(near(area[axis][2][1], 4.0f));
			CHECK(near(area[axis][2][0], 0.0f));
			CHECK(area[axis][1][0] + area[axis][1][1] > 0.0f);
		}
	}

	void test_subtract_boxes_flags_the_new_planes_of_an_unflagged_brush()
	{
		Cvx brush = cube(2, 4);
		brush.flags = PRIM_SKY;
		std::vector<DigBox> boxes;
		merge_cells({ { 2, 2, 2 } }, boxes);
		std::vector<Cvx> pieces;
		subtract_boxes(brush, boxes, 7u, pieces);
		CHECK(pieces.size() == 3);
		for (const Cvx& piece : pieces) {
			CHECK(piece.flags == PRIM_SKY);
			CHECK(piece.plane_flags.size() == piece.planes.size());
			CHECK(planes_flagged(piece, 0) == 6);
			CHECK(planes_flagged(piece, 7u) == piece.planes.size() - 6);
		}
	}

	void test_subtract_boxes_leaves_what_it_doesnt_cut()
	{
		Cvx brush = cube(2, 4);
		brush.flags = PRIM_SKY;
		std::vector<Cvx> out;
		out.push_back(cube(0, 1));
		// one box far off and one that only touches the cube (at x = 4)
		const std::vector<DigBox> away = { { { 10, 10, 10 }, { 11, 11, 11 } }, { { 4, 2, 2 }, { 5, 3, 3 } } };
		subtract_boxes(brush, away, 7u, out);
		CHECK(out.size() == 2);
		if (out.size() == 2) {
			CHECK(same_convex(out[1], brush));
		}
		// one around the whole cube leaves nothing
		out.clear();
		subtract_boxes(brush, { { { 1, 1, 1 }, { 5, 5, 5 } } }, 7u, out);
		CHECK(out.empty());
	}

	void test_subtract_boxes_keeps_each_piece_within_its_bounds()
	{
		// cubes, so every piece is a box: its volume is its bounds' and the pieces add up to what the cells leave
		struct Case
		{
			Cvx               brush;
			std::vector<Cell> cells;
			float             left;  // the volume left of the brush
		};
		const Case cases[] = {
			{ cube(2, 4), { { 2, 2, 2 } }, 7.0f },
			{ cube(1, 5), { { 2, 2, 2 } }, 63.0f },
			{ cube(1, 5), { { 2, 2, 2 }, { 3, 2, 2 }, { 2, 2, 3 } }, 61.0f },
			{ cube(2.25f, 3.75f), { { 3, 3, 3 } }, 2.953125f },
		};
		for (const Case& c : cases) {
			std::vector<DigBox> boxes;
			merge_cells(c.cells, boxes);
			std::vector<Cvx> pieces;
			subtract_boxes(c.brush, boxes, 0, pieces);
			float volume = 0.0f;
			for (const Cvx& piece : pieces) {
				CHECK(fits_bounds(piece));
				volume += bounds_volume(piece);
			}
			CHECK(near(volume, c.left));
		}
	}

	void test_subtract_boxes_drops_slivers()
	{
		const std::uint32_t cut = dig_flags(DIG_PLANKS);
		const std::size_t   plane_counts[3] = { 7, 8, 9 };
		// a cell starting a hair inside the cube's face (less than DIG_SLIVER): no layer of the cube below it, and the plane
		// the cell makes there is the cube's own
		std::vector<Cvx> pieces;
		subtract_boxes(cube(2, 4), { { { 2.0005f, 2, 2 }, { 3, 3, 3 } } }, cut, pieces);
		CHECK(pieces.size() == 3);
		for (std::size_t k = 0; k < pieces.size() && k < 3; ++k) {
			CHECK(pieces[k].planes.size() == plane_counts[k]);
		}
		// and ending a hair inside the far face
		pieces.clear();
		subtract_boxes(cube(2, 4), { { { 3, 3, 3 }, { 3.9995f, 4, 4 } } }, cut, pieces);
		CHECK(pieces.size() == 3);
		// a cell further in leaves its layer
		pieces.clear();
		subtract_boxes(cube(2, 4), { { { 2.01f, 2, 2 }, { 3, 3, 3 } } }, cut, pieces);
		CHECK(pieces.size() == 4);
	}

	/// the box (x0, y0, z0)-(x1, y1, z1) as a convex: its planes are +x, -x, +y, -y, +z, -z
	Cvx box_convex(float x0, float y0, float z0, float x1, float y1, float z1, std::uint32_t flags)
	{
		Cvx cvx;
		cvx.planes.push_back({ 1, 0, 0, -x1 });
		cvx.planes.push_back({ -1, 0, 0, x0 });
		cvx.planes.push_back({ 0, 1, 0, -y1 });
		cvx.planes.push_back({ 0, -1, 0, y0 });
		cvx.planes.push_back({ 0, 0, 1, -z1 });
		cvx.planes.push_back({ 0, 0, -1, z0 });
		cvx.lo[0] = x0, cvx.lo[1] = y0, cvx.lo[2] = z0;
		cvx.hi[0] = x1, cvx.hi[1] = y1, cvx.hi[2] = z1;
		cvx.flags = flags;
		return cvx;
	}

	/// a tree of one plane (n.p = dist): empty in front of it, solid behind it
	BspTree one_plane(float nx, float ny, float nz, float dist)
	{
		BspTree tree;
		tree.planes.push_back({ nx, ny, nz, dist });
		tree.nodes.push_back({ 0, -1, -2 });
		tree.leaf_contents = { 0, BSP_CONTENTS_SOLID };
		return tree;
	}

	/// the contents of the tree at source point (0, 0, z)
	int contents_at(const BspTree& tree, float z)
	{
		const float p[3] = { 0.0f, 0.0f, z };
		return point_contents(tree, p);
	}

	void test_bsp_tree_gives_a_points_contents()
	{
		const BspTree t = one_plane(0, 0, 1, 106);
		CHECK(contents_at(t, 120) == 0);
		CHECK(contents_at(t, 80) == BSP_CONTENTS_SOLID);
		CHECK(contents_at(t, 106) == 0);  // on the plane: in front of it
		CHECK(contents_at(BspTree{}, 80) == 0);

		// a plane that isn't along an axis takes the dot product: 0.6x + 0.8z = 100 here
		const BspTree slanted = one_plane(0.6f, 0, 0.8f, 100);
		const float   front[3] = { 100, 0, 100 }, back[3] = { 20, 0, 50 };
		CHECK(point_contents(slanted, front) == 0);
		CHECK(point_contents(slanted, back) == BSP_CONTENTS_SOLID);

		// two nodes: the first's back is solid, its front goes on to a plane of x
		BspTree two = t;
		two.planes.push_back({ 1, 0, 0, 0 });
		two.nodes[0] = { 0, 1, -2 };
		two.nodes.push_back({ 1, -1, -2 });
		const float east[3] = { 5, 0, 120 }, west[3] = { -5, 0, 120 }, under[3] = { 5, 0, 80 };
		CHECK(point_contents(two, east) == 0);
		CHECK(point_contents(two, west) == BSP_CONTENTS_SOLID);
		CHECK(point_contents(two, under) == BSP_CONTENTS_SOLID);

		// a head node that is a leaf
		BspTree leaf = t;
		leaf.headnode = -2;
		CHECK(contents_at(leaf, 120) == BSP_CONTENTS_SOLID);

		// a damaged tree reads as empty, and never loops
		BspTree broken = t;
		broken.nodes[0][2] = -5;  // a leaf it doesn't have
		CHECK(contents_at(broken, 80) == 0);
		broken = t;
		broken.nodes[0][1] = 3;  // a node it doesn't have
		CHECK(contents_at(broken, 120) == 0);
		broken = t;
		broken.nodes[0][0] = 7;  // a plane it doesn't have
		CHECK(contents_at(broken, 80) == 0);
		broken = t;
		broken.nodes[0] = { 0, 0, 0 };  // a loop
		CHECK(contents_at(broken, 80) == 0);
		broken = t;
		broken.headnode = 4;
		CHECK(contents_at(broken, 80) == 0);
		broken.headnode = -9;
		CHECK(contents_at(broken, 80) == 0);
	}

	/// v's bytes at `at`
	template <typename T>
	void put(std::vector<std::uint8_t>& bytes, std::size_t at, T v)
	{
		std::memcpy(bytes.data() + at, &v, sizeof(v));
	}

	void test_read_bsp_tree()
	{
		// the .bsp's lumps of one node on the plane z = 0: empty in front of it (leaf 0), solid behind it (leaf 1)
		std::vector<std::uint8_t> planes(BSP_PLANE_BYTES), nodes(BSP_NODE_BYTES), leaves(2 * BSP_LEAF_BYTES_V1), leaves_v0(2 * BSP_LEAF_BYTES_V0);
		put(planes, 8, 1.0f);  // dplane_t's normal.z
		put(nodes, 4, -1);     // dnode_t's children: in front, behind
		put(nodes, 8, -2);
		put(leaves, BSP_LEAF_BYTES_V1, BSP_CONTENTS_SOLID);  // dleaf_t's contents
		put(leaves_v0, BSP_LEAF_BYTES_V0, BSP_CONTENTS_SOLID);
		put(leaves_v0, 32, 7);  // leaf 0's ambient light, where version 1's leaf 1 would start

		const float above[3] = { 0, 0, 10 }, below[3] = { 0, 0, -10 };
		BspTree     tree;
		CHECK(read_bsp_tree(planes, nodes, leaves, 1, 0, tree));
		CHECK(tree.planes.size() == 1 && tree.nodes.size() == 1 && tree.leaf_contents.size() == 2 && tree.headnode == 0);
		CHECK(point_contents(tree, above) == 0);
		CHECK(point_contents(tree, below) == BSP_CONTENTS_SOLID);
		CHECK(read_bsp_tree(planes, nodes, leaves_v0, 0, 0, tree));
		CHECK(point_contents(tree, below) == BSP_CONTENTS_SOLID);

		// what it refuses leaves out empty, whatever it held
		const auto refused = [&tree](const std::vector<std::uint8_t>& p, const std::vector<std::uint8_t>& n, const std::vector<std::uint8_t>& l, int version, int headnode) {
			BspTree out = tree;
			out.headnode = 5;
			return !read_bsp_tree(p, n, l, version, headnode, out) && out.planes.empty() && out.nodes.empty() && out.leaf_contents.empty() && out.headnode == 0;
		};
		CHECK(refused(planes, nodes, leaves, 2, 0));                                             // a leaf version it doesn't know
		CHECK(refused(planes, nodes, std::vector<std::uint8_t>(BSP_LEAF_BYTES_V1 + 1), 1, 0));  // a lump that isn't whole leaves
		CHECK(refused(planes, nodes, leaves, 1, 1));                                             // a head node it doesn't have
		std::vector<std::uint8_t> bad = nodes;
		put(bad, 0, 1);  // a plane it doesn't have
		CHECK(refused(planes, bad, leaves, 1, 0));
		bad = nodes;
		put(bad, 8, -3);  // leaf 2, which it doesn't have
		CHECK(refused(planes, bad, leaves, 1, 0));
		bad = nodes;
		put(bad, 8, 0);  // a child that doesn't come after its node (here itself: a loop)
		CHECK(refused(planes, bad, leaves, 1, 0));
	}

	void test_void_region_finds_the_rock_behind_the_walls()
	{
		const MapSlot slot{ 1, 16.0f };
		// the region (128, 0, 0) is source x 0..320, z 16..336 (block bx + 8bz + 64by); a dug wall makes its box diggable
		ColPrimitives prims;
		prims.convexes.push_back(box_convex(1024, 0, 0, 1024.25f, 1, 1, PRIM_DIGGABLE));  // block 0, sub-voxels x 0 and 1
		const float         x0[3] = { 1027, 1029, 1030 };  // blocks 35, 37 and 38
		const std::uint32_t flags[3] = { PRIM_DIGGABLE, PRIM_SKY, 0 };
		for (int k = 0; k < 3; ++k) {
			prims.convexes.push_back(box_convex(x0[k], 0, 4, x0[k] + 1, 1, 5, flags[k]));
		}

		// solid below minecraft y 2.25
		RegionVoid out;
		void_region(prims, one_plane(0, 0, 1, 106), slot, 128, 0, 0, out);
		CHECK(out.any());
		for (int block = 0; block < 512; ++block) {
			std::uint8_t want = VOID_ALL;
			if (block == 0 || (block >= 128 && block < 192)) {
				want = VOID_SOME;
			} else if (block == 35 || block == 37 || block >= 192) {
				want = VOID_NONE;
			}
			CHECK(out.kind[static_cast<std::size_t>(block)] == want);
		}
		if (CHECK(out.layers.size() == 65u)) {
			// block 0 is void but for its diggable sub-voxels x 0 and 1; block 128 is void in its two lowest layers
			for (const std::uint64_t row : out.layers[0]) {
				CHECK(row == 0xFCFCFCFCFCFCFCFCull);
			}
			CHECK(out.layers[1][0] == ~0ull && out.layers[1][1] == ~0ull);
			for (int y = 2; y < 8; ++y) {
				CHECK(out.layers[1][y] == 0);
			}
		}

		// solid west of source x 100 (minecraft x 1026.5), no brushes
		void_region(ColPrimitives{}, one_plane(1, 0, 0, 100), slot, 128, 0, 0, out);
		CHECK(out.kind[0] == VOID_ALL && out.kind[1] == VOID_ALL);
		CHECK(out.kind[2] == VOID_SOME);
		CHECK(out.kind[3] == VOID_NONE);
		if (CHECK(out.layers.size() == 64u)) {
			for (const std::uint64_t row : out.layers[0]) {
				CHECK(row == 0x0F0F0F0F0F0F0F0Full);
			}
		}

		// no solid leaf anywhere: out is reset
		void_region(prims, BspTree{}, slot, 128, 0, 0, out);
		CHECK(!out.any() && out.layers.empty());
	}

	void test_void_region_tests_a_convex_by_its_planes()
	{
		// the half of block 0 with x + z <= 1025 (a wedge, its bounds the whole block) is the wall's: the rest is void
		Cvx wedge = box_convex(1024, 0, 0, 1025, 1, 1, PRIM_DIGGABLE);
		wedge.planes.push_back({ 1, 0, 1, -1025 });
		ColPrimitives prims;
		prims.convexes.push_back(wedge);
		RegionVoid out;
		void_region(prims, one_plane(0, 0, 1, 106), MapSlot{ 1, 16.0f }, 128, 0, 0, out);
		CHECK(out.kind[0] == VOID_SOME);
		if (CHECK(out.layers.size() > 0u)) {
			std::uint64_t want = 0;  // sub-voxel (x, z) is void where its centre has (x + z + 1) / 8 > 1
			for (int z = 0; z < 8; ++z) {
				for (int x = 0; x < 8; ++x) {
					if (x + z >= 8) {
						want |= 1ull << (z * 8 + x);
					}
				}
			}
			for (const std::uint64_t row : out.layers[0]) {
				CHECK(row == want);
			}
		}
	}

	void test_dig_mask_layout_places_the_cells_in_the_texture()
	{
		const DigMaskLayout a = DigMaskLayout::around(0.5, 70.2, -3.0);
		CHECK(a.ox == -64 && a.oy == 32 && a.oz == -80);  // its centre is (0, 64, -16)
		CHECK(!a.needs_recentre(0.5, 70.2, -3.0));

		// recentred past 32 blocks along x or z, or 16 along y, from the centre
		CHECK(!a.needs_recentre(32.0, 64, -16) && !a.needs_recentre(-32.0, 64, -16));
		CHECK(a.needs_recentre(32.5, 64, -16) && a.needs_recentre(-32.5, 64, -16));
		CHECK(!a.needs_recentre(0, 64, 16.0) && a.needs_recentre(0, 64, 16.5));
		CHECK(!a.needs_recentre(0, 64, -48.0) && a.needs_recentre(0, 64, -48.5));
		CHECK(!a.needs_recentre(0, 80.0, -16) && a.needs_recentre(0, 80.5, -16));
		CHECK(!a.needs_recentre(0, 48.0, -16) && a.needs_recentre(0, 47.5, -16));

		// a cell's texel: 64 layers of 128 x 128 in tiles of 8 x 8, the first layer's corner cell at (0, 0)
		int u = -1, v = -1;
		CHECK(a.texel(-64, 32, -80, u, v) && u == 0 && v == 0);
		CHECK(a.texel(-59, 41, -73, u, v) && u == 133 && v == 135);
		CHECK(a.texel(63, 95, 47, u, v) && u == 1023 && v == 1023);
		CHECK(!a.texel(-65, 32, -80, u, v) && !a.texel(64, 32, -80, u, v));
		CHECK(!a.texel(-64, 31, -80, u, v) && !a.texel(-64, 96, -80, u, v));
		CHECK(!a.texel(-64, 32, -81, u, v) && !a.texel(-64, 32, 48, u, v));

		// the shader's constants: a map in slot 1 (minecraft x 1024) with its grid at source z 16
		const MapSlot       slot{ 1, 16.0f };
		const DigMaskLayout b = DigMaskLayout::around(1030.5, 70.2, -3.0);
		CHECK(b.ox == 960 && b.oy == 32 && b.oz == -80);
		float origin[4], layout[4];
		b.shader_constants(slot, origin, layout);
		CHECK(origin[0] == -2560.0f && origin[1] == 3200.0f && origin[2] == 1296.0f);
		CHECK(std::fabs(origin[3] - 0.025f) < 1e-7f);
		CHECK(layout[0] == 1.0f / 1024.0f && layout[1] == 128.0f && layout[2] == 8.0f && layout[3] == 1.0f);

		// the shader's arithmetic on a point of block (965, 41, -73) (the constants, then the distance over 40, floored) finds the cell that texel() puts at that block
		float s[3];
		mc_to_source(965.5, 41.5, -72.5, slot, s);
		const int i = static_cast<int>(std::floor((s[0] - origin[0]) * origin[3]));
		const int j = static_cast<int>(std::floor((origin[1] - s[1]) * origin[3]));
		const int k = static_cast<int>(std::floor((s[2] - origin[2]) * origin[3]));
		CHECK(i == 5 && j == 7 && k == 9);
		CHECK(b.texel(965, 41, -73, u, v) && u == (k % 8) * 128 + i && v == (k / 8) * 128 + j);
	}

	void test_dig_mask_shader_texel()
	{
		// a map in slot 3 (minecraft x 3072) with its grid at source z -152.5
		const DigMaskLayout layout = DigMaskLayout::around(3200.5, 70.2, -40.7);
		CHECK(layout.ox == 3136 && layout.oy == 32 && layout.oz == -112);
		const MapSlot slot{ 3, -152.5f };
		float         o[4], l[4];
		layout.shader_constants(slot, o, l);

		// the centre of block (x, y, z)'s cell, source units
		const auto centre = [&slot](int x, int y, int z, float p[3]) { mc_to_source(x + .5, y + .5, z + .5, slot, p); };
		float      p[3];
		int        u = -1, v = -1;
		centre(3141, 41, -105, p);
		CHECK(shader_texel(o, l, p, u, v) && u == 133 && v == 135);  // i 5, j 7, k 9
		centre(3263, 95, 15, p);
		CHECK(shader_texel(o, l, p, u, v) && u == 1023 && v == 1023);

		// past each of the layout's edges: false, u and v left alone
		u = v = -1;
		centre(3135, 32, -112, p);
		CHECK(!shader_texel(o, l, p, u, v));
		centre(3136, 96, -112, p);
		CHECK(!shader_texel(o, l, p, u, v));
		centre(3136, 32, 16, p);
		CHECK(!shader_texel(o, l, p, u, v));
		CHECK(u == -1 && v == -1);

		// every 3rd cell in and around the layout: the shader finds the texel texel() puts the cell at
		int wrong = 0, inside = 0;
		for (int x = 3134; x <= 3266; x += 3) {
			for (int y = 30; y <= 97; y += 3) {
				for (int z = -114; z <= 18; z += 3) {
					int tu = -1, tv = -1;
					u = v = -1;
					centre(x, y, z, p);
					const bool want = layout.texel(x, y, z, tu, tv);
					const bool got = shader_texel(o, l, p, u, v);
					wrong += got != want || (got && (u != tu || v != tv)) ? 1 : 0;
					inside += want ? 1 : 0;
				}
			}
		}
		CHECK(wrong == 0);
		CHECK(inside > 0);
	}

	void test_dig_mask_write_section()
	{
		const DigMaskLayout       layout = DigMaskLayout::around(3200.5, 70.2, -40.7);
		std::vector<std::uint8_t> rgba(1024 * 1024 * 4, 0);
		std::uint8_t              bits[512] = {};
		const int                 bit = 3 + 16 * 5 + 256 * 9;  // block (3, 9, 5) of the section
		bits[bit / 8] = static_cast<std::uint8_t>(1u << (bit % 8));
		const auto red = [&rgba](int u, int v) { return rgba[(static_cast<std::size_t>(v) * 1024 + u) * 4]; };

		// section (196, 2, -7): blocks from (3136, 32, -112), the layout's corner
		CHECK(write_section(layout, 196, 2, -7, bits, rgba.data()));
		CHECK(red(131, 133) == 255);  // cell (3139, 41, -107)
		const std::size_t texel = (133 * 1024 + 131) * 4;
		CHECK(rgba[texel + 1] == 0 && rgba[texel + 2] == 0 && rgba[texel + 3] == 0);
		CHECK(red(132, 133) == 0 && red(131, 134) == 0 && red(3, 5) == 0);

		// no mask: nothing dug in the section
		CHECK(write_section(layout, 196, 2, -7, nullptr, rgba.data()));
		CHECK(red(131, 133) == 0);

		// a section outside the layout writes nothing
		CHECK(!write_section(layout, 195, 2, -7, bits, rgba.data()));
		CHECK(std::all_of(rgba.begin(), rgba.end(), [](std::uint8_t byte) { return byte == 0; }));
	}

	void test_dig_mask_has_section()
	{
		// blocks (3136, 32, -112) to (3263, 95, 15): sections 196-203 along x, 2-5 along y, -7-0 along z
		const DigMaskLayout layout = DigMaskLayout::around(3200.5, 70.2, -40.7);
		CHECK(layout.has_section(196, 2, -7) && layout.has_section(203, 5, 0) && layout.has_section(200, 4, -3));
		CHECK(!layout.has_section(195, 2, -7) && !layout.has_section(204, 2, -7));
		CHECK(!layout.has_section(196, 1, -7) && !layout.has_section(196, 6, -7));
		CHECK(!layout.has_section(196, 2, -8) && !layout.has_section(196, 2, 1));

		// the sections in and around the layout: write_section writes the ones it has, and only those
		std::vector<std::uint8_t> rgba(1024 * 1024 * 4, 0);
		int                       wrong = 0, inside = 0;
		for (int sx = 194; sx <= 205; ++sx) {
			for (int sy = 0; sy <= 7; ++sy) {
				for (int sz = -9; sz <= 2; ++sz) {
					const bool has = layout.has_section(sx, sy, sz);
					wrong += write_section(layout, sx, sy, sz, nullptr, rgba.data()) != has ? 1 : 0;
					inside += has ? 1 : 0;
				}
			}
		}
		CHECK(wrong == 0);
		CHECK(inside == 8 * 4 * 8);
	}

	void test_dug_cells()
	{
		// section (-1, 0, 2) (blocks from -16, 0, 32): bit 0 is block (0, 0, 0) of it, bit 561 (byte 70, bit 1) block (1, 2, 3)
		std::uint8_t bits[512] = {};
		bits[0] = 1u;
		bits[70] = 1u << 1;
		DugCells          cells;
		std::vector<Cell> changed;
		cells.set(-1, 0, 2, bits, changed);
		CHECK(changed.size() == 2);
		CHECK(cells.count() == 2);
		CHECK(cells.dug(-16, 0, 32) && cells.dug(-15, 2, 35));
		CHECK(!cells.dug(-16, 0, 33) && !cells.dug(0, 0, 32));

		// the same mask changes nothing; a cell filled again is the one change
		changed.clear();
		cells.set(-1, 0, 2, bits, changed);
		CHECK(changed.empty());
		bits[70] = 0;
		cells.set(-1, 0, 2, bits, changed);
		CHECK(changed == (std::vector<Cell>{ { -15, 2, 35 } }));
		CHECK(cells.count() == 1);

		// a box only touching a cell doesn't have it; one inside it does
		std::vector<Cell> in;
		const float       touch_lo[3] = { -15, 0, 32 }, touch_hi[3] = { -14, 1, 33 };
		cells.cells_in(touch_lo, touch_hi, in);
		CHECK(in.empty());
		const float inside_lo[3] = { -15.5f, .5f, 32.5f }, inside_hi[3] = { -15.4f, .6f, 32.6f };
		cells.cells_in(inside_lo, inside_hi, in);
		CHECK(in == (std::vector<Cell>{ { -16, 0, 32 } }));

		// no mask: the section has nothing dug
		changed.clear();
		cells.set(-1, 0, 2, nullptr, changed);
		CHECK(changed.size() == 1);
		CHECK(cells.count() == 0);

		// cells around (0.5, 0.5, 0.5): (1, 0, 0) and (5, 0, 0) and (0, 0, 10) in section (0, 0, 0), (32, 0, 0) in (2, 0, 0)
		std::uint8_t near_bits[512] = {}, far_bits[512] = {};
		near_bits[0] = 1u << 5 | 1u << 1;
		near_bits[20] = 1u;
		far_bits[0] = 1u;
		cells.set(0, 0, 0, near_bits, changed);
		cells.set(2, 0, 0, far_bits, changed);
		std::vector<Cell> nearest;
		cells.nearest({ .5, .5, .5 }, 24.0, 2, nearest);
		CHECK(nearest == (std::vector<Cell>{ { 1, 0, 0 }, { 5, 0, 0 } }));
		cells.nearest({ .5, .5, .5 }, 24.0, 64, nearest);
		CHECK(nearest.size() == 3 && nearest[2] == (Cell{ 0, 0, 10 }));

		// a source box is dug when every cell its volume reaches into is (slot 1, grid at source z 16: cell (1025, 0, -1)
		// is source x 40..80, y 0..40, z 16..56)
		const MapSlot slot{ 1, 16.0f };
		std::uint8_t  column[512] = {};
		column[(1 + 16 * 15) / 8] = 1u << ((1 + 16 * 15) % 8);              // block (1, 0, 15) of section (64, 0, -1)
		column[(1 + 16 * 15 + 256) / 8] = 1u << ((1 + 16 * 15 + 256) % 8);  // and the one above it
		cells.set(64, 0, -1, column, changed);
		const auto  dug_cell = [&cells](int x, int y, int z) { return cells.dug(x, y, z); };
		const float in_lo[3] = { 44, 4, 17 }, in_hi[3] = { 76, 36, 96 };
		CHECK(source_box_dug(in_lo, in_hi, slot, dug_cell));
		const float edge_lo[3] = { 40, 0, 16 }, edge_hi[3] = { 80, 40, 96 };  // exactly the two cells
		CHECK(source_box_dug(edge_lo, edge_hi, slot, dug_cell));
		const float over_lo[3] = { 44, 4, 17 }, over_hi[3] = { 81, 36, 56 };  // a unit into cell (1026, 0, -1)
		CHECK(!source_box_dug(over_lo, over_hi, slot, dug_cell));
		const float up_lo[3] = { 44, 4, 17 }, up_hi[3] = { 76, 36, 97 };  // a unit into cell (1025, 2, -1)
		CHECK(!source_box_dug(up_lo, up_hi, slot, dug_cell));
	}

	void test_triangle_clip()
	{
		const float                     lo[3] = { 0, 0, 0 }, hi[3] = { 1, 1, 1 };
		std::vector<ColPrimitives::Tri> out;

		// a big triangle over the box's middle: what's left is the square of it in the box, wound and flagged as it was
		clip_triangle({ { -4, .5f, -4, -4, .5f, 8, 8, .5f, -4 }, 0x305u }, lo, hi, out);
		CHECK(!out.empty());
		float area = 0.0f;
		for (const auto& tri : out) {
			CHECK(tri.flags == 0x305u);
			for (int k = 0; k < 9; ++k) {
				CHECK(tri.v[k] >= -1e-5f && tri.v[k] <= 1.0f + 1e-5f);
			}
			const auto n = triangle_normal(tri.v);
			CHECK(n[1] > 1e-6f);
			area += length(n) / 2.0f;
		}
		CHECK(near(area, 1.0f, 1e-4f));

		// one inside the box is itself; one outside it is nothing
		const ColPrimitives::Tri inside{ { .25f, .25f, .25f, .25f, .25f, .75f, .75f, .25f, .25f }, 0 };
		out.clear();
		clip_triangle(inside, lo, hi, out);
		CHECK(out.size() == 1 && std::equal(inside.v, inside.v + 9, out[0].v) && out[0].flags == 0);
		out.clear();
		clip_triangle({ { 2, .5f, 0, 2, .5f, 1, 3, .5f, 0 }, 0 }, lo, hi, out);
		CHECK(out.empty());
	}
}

int run_tests()
{
	test_convex_face_gives_a_cube_its_squares();
	test_face_flags_take_the_planes_material();
	test_triangulate_gives_each_face_its_flags();
	test_dig_material_maps_surface_materials();
	test_brush_material();
	test_merge_cells_makes_boxes();
	test_subtract_boxes_cuts_the_cells_voxels();
	test_subtract_boxes_flags_the_cut_faces();
	test_subtract_boxes_flags_the_new_planes_of_an_unflagged_brush();
	test_subtract_boxes_leaves_what_it_doesnt_cut();
	test_subtract_boxes_keeps_each_piece_within_its_bounds();
	test_subtract_boxes_drops_slivers();
	test_bsp_tree_gives_a_points_contents();
	test_read_bsp_tree();
	test_void_region_finds_the_rock_behind_the_walls();
	test_void_region_tests_a_convex_by_its_planes();
	test_dig_mask_layout_places_the_cells_in_the_texture();
	test_dig_mask_shader_texel();
	test_dig_mask_write_section();
	test_dig_mask_has_section();
	test_dug_cells();
	test_triangle_clip();
	if (failed == 0) {
		std::printf("core_tests: %d checks passed\n", checks);
		return 0;
	}
	std::printf("core_tests: %d of %d checks failed\n", failed, checks);
	return 1;
}

int main()
{
	// a check that throws (out of memory) says so instead of crashing
	try {
		return run_tests();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "core_tests: %s\n", error.what());
		return 2;
	}
}
