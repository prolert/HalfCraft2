package dev.halfcraft.client.render;

import static dev.halfcraft.link.Proto.DIG_IRON;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import dev.halfcraft.world.DugBits;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.jspecify.annotations.Nullable;
import org.junit.jupiter.api.Test;

class DigWallsTest {
	private static final long[] FULL = { -1L, -1L, -1L, -1L, -1L, -1L, -1L, -1L };
	// Direction ordinals
	private static final int UP = 1;
	private static final int WEST = 4;

	private record Pos(int x, int y, int z) {
	}

	private record Quad(float[] corners, int face, int material, float[] uv) {
	}

	/** Cells not dug, empty and rockless, except where the sets and the map say otherwise; every face iron. */
	private static final class World implements DigWalls.Cells {
		final Map<Pos, long[]> rock = new HashMap<>();
		final Set<Pos> dug = new HashSet<>();
		final Set<Pos> occluding = new HashSet<>();

		@Override
		public boolean dug(int x, int y, int z) {
			return this.dug.contains(new Pos(x, y, z));
		}

		@Override
		public boolean occludes(int x, int y, int z) {
			return this.occluding.contains(new Pos(x, y, z));
		}

		@Override
		public long @Nullable [] rock(int x, int y, int z) {
			return this.rock.get(new Pos(x, y, z));
		}

		@Override
		public int material(int x, int y, int z) {
			return DIG_IRON;
		}
	}

	/** A world with rock {@code layers} east of the dug cell (1, 2, 3). */
	private static World eastRock(long[] layers) {
		World w = new World();
		w.rock.put(new Pos(2, 2, 3), layers);
		return w;
	}

	private static long[] layers(long each) {
		long[] layers = new long[8];
		Arrays.fill(layers, each);
		return layers;
	}

	/** Digs the cell (x, y, z) of section (sx, sy, sz) and gives the walls DigWalls draws around it. */
	private static List<Quad> walls(World w, int sx, int sy, int sz, int x, int y, int z) {
		w.dug.add(new Pos(x, y, z));
		long[] dug = new long[DugBits.LONGS];
		DugBits.set(dug, DugBits.index(x, y, z));
		List<Quad> quads = new ArrayList<>();
		int count = DigWalls.add(sx, sy, sz, dug, w, (corners, face, material, uv) -> quads.add(new Quad(corners.clone(), face, material, uv.clone())));
		assertEquals(quads.size(), count);
		return quads;
	}

	/** The walls around the dug cell (1, 2, 3) of section (0, 0, 0). */
	private static List<Quad> walls(World w) {
		return walls(w, 0, 0, 0, 1, 2, 3);
	}

	private static double[] minus(float[] c, int a, int b) {
		return new double[] { c[a * 3] - c[b * 3], c[a * 3 + 1] - c[b * 3 + 1], c[a * 3 + 2] - c[b * 3 + 2] };
	}

	private static double[] cross(double[] a, double[] b) {
		return new double[] { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	}

	private static double area(List<Quad> quads) {
		double area = 0.0;
		for (Quad q : quads) {
			double[] n = cross(minus(q.corners, 1, 0), minus(q.corners, 3, 0));
			area += Math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		}
		return area;
	}

	private static double[] normal(Quad q) {
		return cross(minus(q.corners, 1, 0), minus(q.corners, 2, 0));
	}

	/** Corner {@code k}'s coordinate {@code axis} (0 x, 1 y, 2 z). */
	private static float corner(Quad q, int k, int axis) {
		return q.corners[k * 3 + axis];
	}

	private static void assertAllIn(Quad q, int axis, double lo, double hi) {
		for (int k = 0; k < 4; k++) {
			float c = corner(q, k, axis);
			assertTrue(c >= lo && c <= hi, "corner " + k + " axis " + axis + " at " + c + ", not in [" + lo + ", " + hi + "]");
		}
	}

	@Test
	void shouldCoverTheWholeFaceTowardAFullyRockEastNeighbour() {
		List<Quad> quads = walls(eastRock(FULL));
		assertEquals(8, quads.size());
		assertEquals(1.0, area(quads), 1e-6);
		for (Quad q : quads) {
			assertEquals(WEST, q.face);
			assertEquals(DIG_IRON, q.material);
			assertTrue(normal(q)[0] < 0, "faces west, into the dug cell");
			for (int k = 0; k < 4; k++) {
				assertEquals(2.0F, corner(q, k, 0));
			}
			assertAllIn(q, 1, 2, 3);
			assertAllIn(q, 2, 3, 4);
			for (float t : q.uv) {
				assertTrue(t >= 0.0F && t <= 1.0F, "uv " + t);
			}
		}
	}

	@Test
	void shouldCoverTheLowerHalfTowardANeighbourRockInItsLowerHalf() {
		List<Quad> quads = walls(eastRock(new long[] { -1L, -1L, -1L, -1L, 0L, 0L, 0L, 0L }));
		assertEquals(4, quads.size());
		assertEquals(0.5, area(quads), 1e-6);
		for (Quad q : quads) {
			assertAllIn(q, 1, 2.0, 2.5);
			for (int k = 0; k < 4; k++) {
				assertTrue(q.uv[k * 2 + 1] >= 0.5F, "v " + q.uv[k * 2 + 1]);
			}
		}
	}

	@Test
	void shouldUseTheNeighboursSliceThatTouchesTheFace() {
		// x = 7 of every row: the far side of the east neighbour
		assertEquals(0, walls(eastRock(layers(0x8080808080808080L))).size());
		// x = 0: the side against the dug cell
		List<Quad> quads = walls(eastRock(layers(0x0101010101010101L)));
		assertEquals(8, quads.size());
		assertEquals(1.0, area(quads), 1e-6);
	}

	@Test
	void shouldSplitARowIntoItsRuns() {
		// x = 0 at z 0, 1 and 4
		List<Quad> quads = walls(eastRock(layers(0x0000000100000101L)));
		assertEquals(16, quads.size());
		assertEquals(0.375, area(quads), 1e-6);
		for (Quad q : quads) {
			float lo = Float.MAX_VALUE, hi = -Float.MAX_VALUE;
			for (int k = 0; k < 4; k++) {
				lo = Math.min(lo, corner(q, k, 2));
				hi = Math.max(hi, corner(q, k, 2));
			}
			assertTrue((lo == 3.0F && hi == 3.25F) || (lo == 3.5F && hi == 3.625F), "z from " + lo + " to " + hi);
		}
	}

	@Test
	void shouldFloorTheCellWithTheTopSliceOfTheNeighbourBelow() {
		World w = new World();
		long[] top = new long[8];
		top[7] = -1L;
		w.rock.put(new Pos(1, 1, 3), top);
		List<Quad> quads = walls(w);
		assertEquals(8, quads.size());
		assertEquals(1.0, area(quads), 1e-6);
		for (Quad q : quads) {
			assertEquals(UP, q.face);
			assertTrue(normal(q)[1] > 0, "faces up, into the dug cell");
			for (int k = 0; k < 4; k++) {
				assertEquals(2.0F, corner(q, k, 1));
			}
		}
	}

	@Test
	void shouldDrawNoWallTowardADugOccludingOrRocklessNeighbour() {
		World dug = eastRock(FULL);
		dug.dug.add(new Pos(2, 2, 3));
		assertEquals(0, walls(dug).size());
		World occluding = eastRock(FULL);
		occluding.occluding.add(new Pos(2, 2, 3));
		assertEquals(0, walls(occluding).size());
		assertEquals(0, walls(new World()).size());
		assertEquals(0, walls(eastRock(new long[8])).size());
	}

	@Test
	void shouldDrawNoWallsInADugCellAnOccludingBlockFills() {
		World w = eastRock(FULL);
		w.occluding.add(new Pos(1, 2, 3));
		assertEquals(0, walls(w).size());
	}

	@Test
	void shouldGiveSectionRelativeCornersAcrossASectionEdge() {
		World w = new World();
		w.rock.put(new Pos(32, -14, 35), FULL);
		List<Quad> quads = walls(w, 1, -1, 2, 31, -14, 35);
		assertEquals(8, quads.size());
		for (Quad q : quads) {
			for (int k = 0; k < 4; k++) {
				assertEquals(16.0F, corner(q, k, 0));
			}
			assertAllIn(q, 1, 2, 3);
			assertAllIn(q, 2, 3, 4);
		}
	}
}
