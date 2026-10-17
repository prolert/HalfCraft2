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
#include <exception>
#include <vector>

#include "core/hc_collision_shapes.h"
#include "core/hc_dig_material.h"
#include "core/hc_dig_clip.h"

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
}

int run_tests()
{
	test_convex_face_gives_a_cube_its_squares();
	test_face_flags_take_the_planes_material();
	test_triangulate_gives_each_face_its_flags();
	test_dig_material_maps_surface_materials();
	test_merge_cells_makes_boxes();
	test_subtract_boxes_cuts_the_cells_voxels();
	test_subtract_boxes_flags_the_cut_faces();
	test_subtract_boxes_flags_the_new_planes_of_an_unflagged_brush();
	test_subtract_boxes_leaves_what_it_doesnt_cut();
	test_subtract_boxes_keeps_each_piece_within_its_bounds();
	test_subtract_boxes_drops_slivers();
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
