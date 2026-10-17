// core_tests: plain checks over the pure c++ core (source/src/core's headers: no sdk, no engine, no link), the code
// server.dll and client.dll share with no engine under it. built and run by tools/debug/core_tests.py (make core-test).
//
// CHECK(condition) counts a check and, when it fails, says which one and goes on: one run shows everything wrong.
// each test is a function named for what it holds; run_tests calls them all and returns the exit code.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

#include "core/hc_collision_shapes.h"

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
}

int run_tests()
{
	test_convex_face_gives_a_cube_its_squares();
	test_face_flags_take_the_planes_material();
	test_triangulate_gives_each_face_its_flags();
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
