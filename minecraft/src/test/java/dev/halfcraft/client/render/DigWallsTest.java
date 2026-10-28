package dev.halfcraft.client.render;

import static dev.halfcraft.link.Proto.DIG_IRON;
import static dev.halfcraft.link.Proto.TRI_DIGGABLE;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import dev.halfcraft.world.DugBits;
import dev.halfcraft.world.HostTri;
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
	// a world brush's face, iron
	private static final int BRUSH = TRI_DIGGABLE | DIG_IRON << HostTri.MATERIAL_SHIFT;
	// Direction ordinals
	private static final int DOWN = 0;
	private static final int UP = 1;
	private static final int NORTH = 2;
	private static final int SOUTH = 3;
	private static final int WEST = 4;
	private static final int EAST = 5;

	private record Pos(int x, int y, int z) {
	}

	private record Quad(float[] corners, int face, int material, float[] uv) {
	}

	/** Cells not dug, empty and rockless (their region not known), except where the sets and the map say otherwise; every face iron. */
	private static final class World implements DigWalls.Cells {
		final Map<Pos, long[]> rock = new HashMap<>();
		final Set<Pos> dug = new HashSet<>();
		final Set<Pos> occluding = new HashSet<>();
		final List<HostTri> tris = new ArrayList<>();
		DigWalls.Count count = new DigWalls.Count(0, 0, 0, 0L); // what the walls drew last

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

		@Override
		public void triangles(double minX, double minY, double minZ, double maxX, double maxY, double maxZ, List<HostTri> out) {
			for (HostTri t : this.tris) {
				if (t.maxX >= minX && t.minX <= maxX && t.maxY >= minY && t.minY <= maxY && t.maxZ >= minZ && t.minZ <= maxZ) {
					out.add(t);
				}
			}
		}

		/** A face of Half-Life's (flags: its Proto.TRI_*): the quad a, b, c, d, wound so its normal ((b - a) x (c - a)) points out of the brush. */
		void face(int flags, double[] a, double[] b, double[] c, double[] d) {
			this.tris.add(new HostTri(floats(a, b, c), 0, flags));
			this.tris.add(new HostTri(floats(a, c, d), 0, flags));
		}

		/** Known cells with no rock. */
		void empty(Pos... cells) {
			for (Pos p : cells) {
				this.rock.put(p, new long[8]);
			}
		}
	}

	private static float[] floats(double[]... corners) {
		float[] v = new float[corners.length * 3];
		for (int k = 0; k < v.length; k++) {
			v[k] = (float) corners[k / 3][k % 3];
		}
		return v;
	}

	private static double[] p(double x, double y, double z) {
		return new double[] { x, y, z };
	}

	/** Every row layer of a cell's rock alike: bit z * 8 + x of {@code rows} for each of the 8 layers from {@code y0} up, none below. */
	private static long[] from(int y0, long rows) {
		long[] layers = new long[8];
		for (int y = y0; y < 8; y++) {
			layers[y] = rows;
		}
		return layers;
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
		w.count = DigWalls.add(sx, sy, sz, dug, w, (corners, face, material, uv) -> quads.add(new Quad(corners.clone(), face, material, uv.clone())));
		assertEquals(quads.size(), w.count.quads());
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

	private static double length(double[] v) {
		return Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	/** The quads' area: each as its two triangles (a cut sub-voxel's triangle repeats its last corner). */
	private static double area(List<Quad> quads) {
		double area = 0.0;
		for (Quad q : quads) {
			area += 0.5 * length(cross(minus(q.corners, 1, 0), minus(q.corners, 2, 0)));
			area += 0.5 * length(cross(minus(q.corners, 2, 0), minus(q.corners, 3, 0)));
		}
		return area;
	}

	/** The least of the quads' corners' coordinate {@code axis}. */
	private static double min(List<Quad> quads, int axis) {
		double min = Double.MAX_VALUE;
		for (Quad q : quads) {
			for (int k = 0; k < 4; k++) {
				min = Math.min(min, corner(q, k, axis));
			}
		}
		return min;
	}

	/** The greatest of the quads' corners' coordinate {@code axis}. */
	private static double max(List<Quad> quads, int axis) {
		double max = -Double.MAX_VALUE;
		for (Quad q : quads) {
			for (int k = 0; k < 4; k++) {
				max = Math.max(max, corner(q, k, axis));
			}
		}
		return max;
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

	/** A ceiling's underside at {@code y} across the east neighbour and around it (x 0-5, z 0-7), facing down. */
	private static void ceiling(World w, int flags, double y) {
		w.face(flags, p(0, y, 0), p(5, y, 0), p(5, y, 7), p(0, y, 7));
	}

	@Test
	void shouldEndAHoleInACeilingAtItsUnderside() {
		// the underside at 2.3: the ceiling's sub-voxels reach down to 2.25
		World w = eastRock(from(2, -1L));
		ceiling(w, BRUSH, 2.3);
		List<Quad> quads = walls(w);
		assertEquals(6, quads.size());
		assertEquals(1, w.count.cut(), "the row with the underside in it");
		assertEquals(2, w.count.triangles(), "the underside's two triangles");
		assertEquals(0.7, area(quads), 1e-5);
		assertEquals(2.3, min(quads, 1), 1e-5);
		for (Quad q : quads) {
			assertEquals(WEST, q.face);
			assertEquals(DIG_IRON, q.material);
			assertTrue(normal(q)[0] < 0, "faces west, into the dug cell");
			assertAllIn(q, 0, 2, 2);
			for (int k = 0; k < 4; k++) {
				assertEquals(corner(q, k, 2) - 3, q.uv[k * 2], 1e-5, "u across the face");
				assertEquals(3 - corner(q, k, 1), q.uv[k * 2 + 1], 1e-5, "v down the face");
			}
		}
	}

	@Test
	void shouldNotCutAlongTheFacesTheDigCutsOnTheGrid() {
		// Half-Life's cut of the dug cell out of the ceiling: its piece's face on the wall itself, and faces along the wall's edges
		World w = eastRock(from(2, -1L));
		ceiling(w, BRUSH, 2.3);
		w.face(BRUSH, p(2, 2.3, 3), p(2, 2.3, 4), p(2, 3, 4), p(2, 3, 3)); // on the wall, toward the dug cell
		w.face(BRUSH, p(2, 2.3, 3), p(2, 3, 3), p(3, 3, 3), p(3, 2.3, 3)); // along its edge z 3, toward less z
		w.face(BRUSH, p(2, 2.3, 4), p(3, 2.3, 4), p(3, 3, 4), p(2, 3, 4)); // along its edge z 4, toward more z
		w.face(BRUSH, p(2, 3, 3), p(2, 3, 4), p(3, 3, 4), p(3, 3, 3)); // along its top, toward more y
		List<Quad> quads = walls(w);
		assertEquals(6, quads.size());
		assertEquals(0.7, area(quads), 1e-5);
		assertEquals(2.3, min(quads, 1), 1e-5);
	}

	@Test
	void shouldEndTheSideOfADoorwayAtTheWallsFace() {
		// a wall from z 3.4 on, its face toward the room at less z: its sub-voxels reach back to 3.375
		World w = eastRock(from(0, 0xFFFFFFFFFF000000L));
		w.face(BRUSH, p(0, 0, 3.4), p(0, 5, 3.4), p(5, 5, 3.4), p(5, 0, 3.4));
		List<Quad> quads = walls(w);
		assertEquals(16, quads.size());
		assertEquals(0.6, area(quads), 1e-5);
		assertEquals(3.4, min(quads, 2), 1e-5);
		assertEquals(4.0, max(quads, 2), 1e-6);
	}

	@Test
	void shouldNotCutAlongAFaceInsideTheRock() {
		// a brush's top at 2.55 under another brush
		World w = eastRock(FULL);
		w.face(BRUSH, p(0, 2.55, 0), p(0, 2.55, 7), p(5, 2.55, 7), p(5, 2.55, 0));
		List<Quad> quads = walls(w);
		assertEquals(8, quads.size());
		assertEquals(1.0, area(quads), 1e-6);
	}

	@Test
	void shouldNotCutAlongAFaceThatIsNotDiggable() {
		// a prop's or a displacement's: which side of it is open isn't known
		World w = eastRock(from(2, -1L));
		ceiling(w, 0, 2.3);
		List<Quad> quads = walls(w);
		assertEquals(6, quads.size());
		assertEquals(0.75, area(quads), 1e-6);
		assertEquals(2.25, min(quads, 1), 1e-6);
	}

	@Test
	void shouldLeaveTheSubVoxelAtAnInnerCornerWhole() {
		// the underside at 2.3 over a wall up to z 3.4, the wall's face toward the room at more z
		World w = eastRock(from(2, -1L));
		long[] rock = w.rock.get(new Pos(2, 2, 3));
		rock[0] = 0x00000000FFFFFFFFL;
		rock[1] = 0x00000000FFFFFFFFL;
		ceiling(w, BRUSH, 2.3);
		w.face(BRUSH, p(0, 0, 3.4), p(5, 0, 3.4), p(5, 2.3, 3.4), p(0, 2.3, 3.4));
		List<Quad> quads = walls(w);
		assertEquals(11, quads.size());
		// the wall cut at its face below the ceiling, the ceiling at its underside past the wall, the sub-voxel at the corner whole
		assertEquals(0.4 * 0.25 + 0.5 * 0.125 + 0.5 * 0.075 + 0.625, area(quads), 1e-5);
	}

	@Test
	void shouldCutBothFacesAtABrushsOuterCorner() {
		// a beam up to z 3.4 with its underside at 2.3: faces down and toward more z
		World w = eastRock(from(2, 0x00000000FFFFFFFFL));
		w.face(BRUSH, p(0, 2.3, 0), p(5, 2.3, 0), p(5, 2.3, 3.4), p(0, 2.3, 3.4));
		w.face(BRUSH, p(0, 2.3, 3.4), p(5, 2.3, 3.4), p(5, 5, 3.4), p(0, 5, 3.4));
		List<Quad> quads = walls(w);
		assertEquals(12, quads.size());
		assertEquals(0.4 * 0.7, area(quads), 1e-5);
		assertEquals(2.3, min(quads, 1), 1e-5);
		assertEquals(3.4, max(quads, 2), 1e-5);
	}

	@Test
	void shouldNotCutAwayABrushUnderAnotherOnesFace() {
		// the underside at 2.3 over everything; under it from z 3.45 on, a brush down to 2.27 (its underside, and its side toward less z)
		World w = eastRock(from(2, -1L));
		ceiling(w, BRUSH, 2.3);
		w.face(BRUSH, p(0, 2.27, 3.45), p(5, 2.27, 3.45), p(5, 2.27, 7), p(0, 2.27, 7));
		w.face(BRUSH, p(0, 2.27, 3.45), p(0, 2.3, 3.45), p(5, 2.3, 3.45), p(5, 2.27, 3.45));
		List<Quad> quads = walls(w);
		// cut at 2.3 before the step and at 2.27 past it, the sub-voxel with the step (z 3.375-3.5) whole
		assertEquals(0.375 * 0.075 + 0.125 * 0.125 + 0.5 * 0.105 + 0.625, area(quads), 1e-5);
		List<Quad> before = new ArrayList<>(), past = new ArrayList<>();
		for (Quad q : quads) {
			if (max(List.of(q), 2) <= 3.375 + 1e-6) {
				before.add(q);
			} else if (min(List.of(q), 2) >= 3.5 - 1e-6) {
				past.add(q);
			}
		}
		assertEquals(2.3, min(before, 1), 1e-5);
		assertEquals(2.27, min(past, 1), 1e-5);
	}

	@Test
	void shouldLookForTheOpenAirPastTheFacesEdge() {
		// the underside at 2.05, in the face's lowest sub-voxels: the room under it is in the cell under the neighbour
		World unknown = eastRock(FULL);
		ceiling(unknown, BRUSH, 2.05);
		assertEquals(1.0, area(walls(unknown)), 1e-6); // that cell's region hasn't come: left whole
		World w = eastRock(FULL);
		ceiling(w, BRUSH, 2.05);
		w.empty(new Pos(2, 1, 3));
		List<Quad> quads = walls(w);
		assertEquals(0.95, area(quads), 1e-5);
		assertEquals(2.05, min(quads, 1), 1e-5);
		// a dug cell there tells nothing of what was in it (a revealed one was rock throughout): left whole
		World dug = eastRock(FULL);
		ceiling(dug, BRUSH, 2.05);
		dug.dug.add(new Pos(2, 1, 3));
		assertEquals(1.0, area(walls(dug)), 1e-6);
	}

	@Test
	void shouldNotCutAFaceOpenOntoWhatARevealedCellCutAway() {
		// a floor's underside at 2.05 over the void beyond the map, under the neighbour, and the cell under that revealed
		World overVoid = eastRock(FULL);
		ceiling(overVoid, BRUSH, 2.05);
		overVoid.rock.put(new Pos(2, 1, 3), FULL);
		overVoid.dug.add(new Pos(2, 1, 3));
		assertEquals(1.0, area(walls(overVoid)), 1e-6);
		// over a brush from 2 to 2.05 instead, cut back at 2 by that cell's dig: its cap faces the dug cell
		World overBrush = eastRock(FULL);
		ceiling(overBrush, BRUSH, 2.05);
		overBrush.face(BRUSH, p(0, 2.05, 0), p(0, 2.05, 7), p(5, 2.05, 7), p(5, 2.05, 0)); // its top, against the floor
		overBrush.face(BRUSH, p(0, 2, 0), p(5, 2, 0), p(5, 2, 7), p(0, 2, 7)); // its cap
		overBrush.dug.add(new Pos(2, 1, 3));
		assertEquals(1.0, area(walls(overBrush)), 1e-6);
		assertEquals(0, overBrush.count.cut());
	}

	@Test
	void shouldNotCutOpenAThinBrushOnAnothersFace() {
		// a brush from 2.25 to 2.3 under the ceiling's underside at 2.3: its underside right on the sub-voxels' boundary, so
		// the voxels leave the row under it out, and the ceiling's underside lies inside the rock
		World w = eastRock(from(2, -1L));
		ceiling(w, BRUSH, 2.3);
		w.face(BRUSH, p(0, 2.25, 0), p(5, 2.25, 0), p(5, 2.25, 7), p(0, 2.25, 7)); // its underside
		w.face(BRUSH, p(0, 2.3, 0), p(0, 2.3, 7), p(5, 2.3, 7), p(5, 2.3, 0)); // its top, against the ceiling
		List<Quad> quads = walls(w);
		assertEquals(6, quads.size());
		assertEquals(0.75, area(quads), 1e-6);
		assertEquals(0, w.count.cut());
	}

	@Test
	void shouldLeaveWholeASubVoxelAPropReachesInto() {
		// a prop's face (in both windings, as they come) hanging under the ceiling's underside at z 3.45, from 2.26 up
		World w = eastRock(from(2, -1L));
		ceiling(w, BRUSH, 2.3);
		w.face(0, p(1.5, 2.26, 3.45), p(2.5, 2.26, 3.45), p(2.5, 2.3, 3.45), p(1.5, 2.3, 3.45));
		w.face(0, p(1.5, 2.26, 3.45), p(1.5, 2.3, 3.45), p(2.5, 2.3, 3.45), p(2.5, 2.26, 3.45));
		List<Quad> quads = walls(w);
		// the sub-voxel at z 3.375-3.5 whole, the other seven of its row cut at 2.3
		assertEquals(0.125 * 0.125 + 7 * 0.125 * 0.075 + 0.625, area(quads), 1e-5);
	}

	@Test
	void shouldCutACeilingAlongAWallInTheCellOverIt() {
		// a wall from x 1.4 on over the dug cell, its face toward less x
		World w = new World();
		w.rock.put(new Pos(1, 3, 3), layers(0xF8F8F8F8F8F8F8F8L));
		w.face(BRUSH, p(1.4, 0, 0), p(1.4, 0, 7), p(1.4, 5, 7), p(1.4, 5, 0));
		List<Quad> quads = walls(w);
		assertEquals(16, quads.size());
		assertEquals(0.6, area(quads), 1e-5);
		assertEquals(1.4, min(quads, 0), 1e-5);
		for (Quad q : quads) {
			assertEquals(DOWN, q.face);
			assertTrue(normal(q)[1] < 0, "faces down, into the dug cell");
			assertAllIn(q, 1, 3, 3);
			assertAllIn(q, 2, 3, 4);
		}
	}

	@Test
	void shouldCutANorthWallAlongAWallsFace() {
		// a wall from x 1.4 on north of the dug cell, its face toward less x
		World w = new World();
		w.rock.put(new Pos(1, 2, 2), layers(0xF8F8F8F8F8F8F8F8L));
		w.face(BRUSH, p(1.4, 0, 0), p(1.4, 0, 7), p(1.4, 5, 7), p(1.4, 5, 0));
		List<Quad> quads = walls(w);
		assertEquals(16, quads.size());
		assertEquals(0.6, area(quads), 1e-5);
		assertEquals(1.4, min(quads, 0), 1e-5);
		for (Quad q : quads) {
			assertEquals(SOUTH, q.face);
			assertTrue(normal(q)[2] > 0, "faces south, into the dug cell");
			assertAllIn(q, 2, 3, 3);
			assertAllIn(q, 1, 2, 3);
		}
	}

	@Test
	void shouldCutAWestWallAlongAWallsFace() {
		// a wall from z 3.4 on west of the dug cell, its face toward the room at less z
		World w = new World();
		w.rock.put(new Pos(0, 2, 3), from(0, 0xFFFFFFFFFF000000L));
		w.face(BRUSH, p(-1, 0, 3.4), p(-1, 5, 3.4), p(3, 5, 3.4), p(3, 0, 3.4));
		List<Quad> quads = walls(w);
		assertEquals(16, quads.size());
		assertEquals(0.6, area(quads), 1e-5);
		assertEquals(3.4, min(quads, 2), 1e-5);
		for (Quad q : quads) {
			assertEquals(EAST, q.face);
			assertTrue(normal(q)[0] > 0, "faces east, into the dug cell");
			assertAllIn(q, 0, 1, 1);
			assertAllIn(q, 2, 3.4 - 1e-5, 4);
		}
	}

	/** Rock east of the dug cell from 2.25 up, and a face through y 2.3 at the wall whose normal is (-sqrt(1 - s^2), -s, 0). */
	private static World steep(double s) {
		World w = eastRock(from(2, -1L));
		double k = Math.sqrt(1 - s * s) / s * 0.05; // how far it rises over 0.05 along x
		w.face(BRUSH, p(1.95, 2.3 + k, 0), p(2.05, 2.3 - k, 0), p(2.05, 2.3 - k, 7), p(1.95, 2.3 + k, 7));
		return w;
	}

	@Test
	void shouldCutOnlyAlongFacesThatCrossTheWall() {
		// its normal 0.06 of the way along the wall: it crosses the wall at 2.3 - 0.002 k (1e-4 into the neighbour), and cuts
		double k = Math.sqrt(1 - 0.06 * 0.06) / 0.06 * 0.05;
		assertEquals(0.7 + 0.002 * k, area(walls(steep(0.06))), 1e-5);
		// 0.04 of the way: it runs along the wall, and cuts nothing
		assertEquals(0.75, area(walls(steep(0.04))), 1e-6);
	}

	@Test
	void shouldCutAlongAFaceWhosePiecesFloatsPutApart() {
		// the south wall's face again, out at x 5001.4, its two triangles a float apart there (2^-11 blocks), as the pieces
		// of one face computed apart come
		World w = new World();
		w.rock.put(new Pos(5001, 2, 4), layers(0x0F0F0F0F0F0F0F0FL));
		float x0 = 5001.4F, x1 = Math.nextUp(x0);
		w.tris.add(new HostTri(new float[] { x0, 0, 0, x0, 5, 0, x0, 5, 7 }, 0, BRUSH));
		w.tris.add(new HostTri(new float[] { x1, 0, 0, x1, 5, 7, x1, 0, 7 }, 0, BRUSH));
		List<Quad> quads = walls(w, 312, 0, 0, 5001, 2, 3);
		assertEquals(16, quads.size());
		assertEquals(8, w.count.cut(), "a cut in every row, the triangles' join too");
		assertEquals(0.4, area(quads), 1e-3);
		assertEquals(5001.4 - 16 * 312, max(quads, 0), 1e-3);
	}

	@Test
	void shouldCutAFloorAlongAWallInTheCellUnderIt() {
		// a wall from x 1.4 on under the dug cell, its face toward less x
		World w = new World();
		w.rock.put(new Pos(1, 1, 3), layers(0xF8F8F8F8F8F8F8F8L));
		w.face(BRUSH, p(1.4, 0, 0), p(1.4, 0, 7), p(1.4, 5, 7), p(1.4, 5, 0));
		List<Quad> quads = walls(w);
		assertEquals(16, quads.size());
		assertEquals(0.6, area(quads), 1e-5);
		assertEquals(1.4, min(quads, 0), 1e-5);
		for (Quad q : quads) {
			assertEquals(UP, q.face);
			assertTrue(normal(q)[1] > 0, "faces up, into the dug cell");
			assertAllIn(q, 1, 2, 2);
			assertAllIn(q, 2, 3, 4);
		}
	}

	@Test
	void shouldCutASouthWallAlongAWallsFace() {
		// a wall up to x 1.4 south of the dug cell, its face toward more x
		World w = new World();
		w.rock.put(new Pos(1, 2, 4), layers(0x0F0F0F0F0F0F0F0FL));
		w.face(BRUSH, p(1.4, 0, 0), p(1.4, 5, 0), p(1.4, 5, 7), p(1.4, 0, 7));
		List<Quad> quads = walls(w);
		assertEquals(16, quads.size());
		assertEquals(0.4, area(quads), 1e-5);
		assertEquals(1.4, max(quads, 0), 1e-5);
		for (Quad q : quads) {
			assertEquals(NORTH, q.face);
			assertTrue(normal(q)[2] < 0, "faces north, into the dug cell");
			assertAllIn(q, 2, 4, 4);
			assertAllIn(q, 0, 1, 1.5);
		}
	}

	@Test
	void shouldCutAlongASlopeInTriangles() {
		// a slope rising toward more z (y - 2 = z - 3), rock above it: its face toward less y and more z
		World w = new World();
		long[] rock = new long[8];
		for (int y = 0; y < 8; y++) {
			for (int z = 0; z <= y; z++) {
				rock[y] |= 0xFFL << (z * 8);
			}
		}
		w.rock.put(new Pos(2, 2, 3), rock);
		w.empty(new Pos(2, 1, 3), new Pos(2, 2, 4));
		w.face(BRUSH, p(0, 2, 3), p(5, 2, 3), p(5, 3, 4), p(0, 3, 4));
		List<Quad> quads = walls(w);
		// a row of whole sub-voxels and a triangle on the slope, each
		assertEquals(15, quads.size());
		assertEquals(0.5, area(quads), 1e-5);
		for (Quad q : quads) {
			for (int k = 0; k < 4; k++) {
				assertTrue(corner(q, k, 1) - 2 >= corner(q, k, 2) - 3 - 1e-5, "above the slope");
			}
		}
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
