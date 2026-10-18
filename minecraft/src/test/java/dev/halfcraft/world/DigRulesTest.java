package dev.halfcraft.world;

import static dev.halfcraft.link.Proto.*;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.jspecify.annotations.Nullable;
import org.junit.jupiter.api.Test;

class DigRulesTest {
	private static final long[] FULL = { -1L, -1L, -1L, -1L, -1L, -1L, -1L, -1L };
	private static final long[] LOWER = { -1L, -1L, -1L, -1L, 0L, 0L, 0L, 0L };
	private static final long[] UPPER = { 0L, 0L, 0L, 0L, -1L, -1L, -1L, -1L };

	private record Pos(int x, int y, int z) {
	}

	/** Cells known, not sky, not dug, air and rockless, except where the sets and maps say otherwise. */
	private static final class World implements DigRules.Cells {
		final Set<Pos> unknown = new HashSet<>();
		final Set<Pos> sky = new HashSet<>();
		final Set<Pos> dug = new HashSet<>();
		final Set<Pos> built = new HashSet<>();
		final Map<Pos, long[]> occupancy = new HashMap<>();
		final Map<Pos, long[]> voids = new HashMap<>();

		@Override
		public boolean known(int x, int y, int z) {
			return !this.unknown.contains(new Pos(x, y, z));
		}

		@Override
		public boolean sky(int x, int y, int z) {
			return this.sky.contains(new Pos(x, y, z));
		}

		@Override
		public boolean dug(int x, int y, int z) {
			return this.dug.contains(new Pos(x, y, z));
		}

		@Override
		public boolean air(int x, int y, int z) {
			return !this.built.contains(new Pos(x, y, z));
		}

		@Override
		public long @Nullable [] occupancy(int x, int y, int z) {
			return this.occupancy.get(new Pos(x, y, z));
		}

		@Override
		public long @Nullable [] voids(int x, int y, int z) {
			return this.voids.get(new Pos(x, y, z));
		}
	}

	/** The layers with one sub-voxel of the top layer taken out. */
	private static long[] lessOne(long[] layers) {
		long[] out = layers.clone();
		out[7] = 0x7FFFFFFFFFFFFFFFL;
		return out;
	}

	@Test
	void shouldRevealAWhollyRockNeighbourWithTheMiningMaterial() {
		World w = new World();
		w.occupancy.put(new Pos(1, 0, 0), FULL);
		assertEquals(List.of(new DigRules.Reveal(1, 0, 0, DIG_IRON)), DigRules.reveals(w, 0, 0, 0, DIG_IRON, false));
	}

	@Test
	void shouldRevealAMostlyVoidNeighbourAsStone() {
		World w = new World();
		w.occupancy.put(new Pos(0, -1, 0), LOWER);
		w.voids.put(new Pos(0, -1, 0), UPPER);
		assertEquals(List.of(new DigRules.Reveal(0, -1, 0, DIG_STONE)), DigRules.reveals(w, 0, 0, 0, DIG_PLANKS, false));
	}

	@Test
	void shouldLeaveAPartlyRockNeighbour() {
		World w = new World();
		w.occupancy.put(new Pos(0, 0, 1), lessOne(FULL));
		assertEquals(List.of(), DigRules.reveals(w, 0, 0, 0, DIG_STONE, false));
	}

	@Test
	void shouldLeaveDugBuiltAndUnknownNeighbours() {
		World w = new World();
		for (Pos p : List.of(new Pos(-1, 0, 0), new Pos(0, 0, -1), new Pos(0, 1, 0))) {
			w.occupancy.put(p, FULL);
		}
		w.dug.add(new Pos(-1, 0, 0));
		w.built.add(new Pos(0, 0, -1));
		w.unknown.add(new Pos(0, 1, 0));
		assertEquals(List.of(), DigRules.reveals(w, 0, 0, 0, DIG_STONE, false));
	}

	@Test
	void shouldLeaveAWhollyRockNeighbourInTheMapsSkyButRevealItsTwinOutOfIt() {
		World w = new World();
		w.occupancy.put(new Pos(0, 1, 0), FULL); // inside the skybox's brush: its voxels are rock too
		w.sky.add(new Pos(0, 1, 0));
		w.occupancy.put(new Pos(0, -1, 0), FULL);
		assertEquals(List.of(new DigRules.Reveal(0, -1, 0, DIG_IRON)), DigRules.reveals(w, 0, 0, 0, DIG_IRON, false));
	}

	@Test
	void shouldRevealASkyNeighbourThatIsPartlyVoid() {
		// the outside of the skybox's brush: its voxels the sky's, the rest of it beyond the map
		World w = new World();
		w.occupancy.put(new Pos(0, 1, 0), LOWER);
		w.voids.put(new Pos(0, 1, 0), UPPER);
		w.sky.add(new Pos(0, 1, 0));
		assertEquals(List.of(new DigRules.Reveal(0, 1, 0, DIG_STONE)), DigRules.reveals(w, 0, 0, 0, DIG_IRON, false));
	}

	@Test
	void shouldRevealOnlyNeighboursWithoutBrushesWhileNothingMoreMayBeDug() {
		World w = new World();
		w.voids.put(new Pos(-1, 0, 0), FULL); // void only, beyond the map
		w.occupancy.put(new Pos(1, 0, 0), FULL); // inside a thick wall
		w.occupancy.put(new Pos(0, 0, 1), LOWER); // a wall's back, void behind it
		w.voids.put(new Pos(0, 0, 1), UPPER);
		assertEquals(List.of(new DigRules.Reveal(-1, 0, 0, DIG_STONE)), DigRules.reveals(w, 0, 0, 0, DIG_IRON, true));
		assertEquals(List.of(new DigRules.Reveal(0, 0, 1, DIG_STONE), new DigRules.Reveal(-1, 0, 0, DIG_STONE), new DigRules.Reveal(1, 0, 0, DIG_IRON)),
			DigRules.reveals(w, 0, 0, 0, DIG_IRON, false));
	}

	@Test
	void shouldRevealEverySideDownUpNorthSouthWestEast() {
		World w = new World();
		List<Pos> sides = List.of(new Pos(5, 5, 7), new Pos(5, 7, 7), new Pos(5, 6, 6), new Pos(5, 6, 8), new Pos(4, 6, 7), new Pos(6, 6, 7));
		for (Pos p : sides) {
			w.occupancy.put(p, FULL);
		}
		List<DigRules.Reveal> expected = sides.stream().map(p -> new DigRules.Reveal(p.x(), p.y(), p.z(), DIG_DIRT)).toList();
		assertEquals(expected, DigRules.reveals(w, 5, 6, 7, DIG_DIRT, false));
	}

	@Test
	void shouldCountOccupancyAndVoidTogetherAsRock() {
		assertTrue(DigRules.whollyRock(FULL, null));
		assertTrue(DigRules.whollyRock(null, FULL));
		assertTrue(DigRules.whollyRock(LOWER, UPPER));
		assertFalse(DigRules.whollyRock(LOWER, null));
		assertFalse(DigRules.whollyRock(null, null));
	}

	@Test
	void shouldTakeStoneFromHalfVoidUpAndTheOpeningsMaterialBelow() {
		assertEquals(DIG_STONE, DigRules.revealMaterial(UPPER, DIG_GLASS));
		assertEquals(DIG_GLASS, DigRules.revealMaterial(lessOne(UPPER), DIG_GLASS));
		assertEquals(DIG_IRON, DigRules.revealMaterial(null, DIG_IRON));
		assertEquals(DIG_STONE, DigRules.revealMaterial(null, DIG_NONE));
		assertEquals(DIG_STONE, DigRules.revealMaterial(null, 99));
	}
}
