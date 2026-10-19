package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class FluidCellsTest {
	private static final long ALL = -1L;

	/** Voxel layers filled from layer {@code lo} up to {@code hi} (inclusive) over the whole cell. */
	private static long[] solid(int lo, int hi) {
		long[] layers = new long[8];
		for (int y = lo; y <= hi; y++) {
			layers[y] = ALL;
		}
		return layers;
	}

	/** Every voxel the layers leave out. */
	private static long[] not(long[] layers) {
		long[] out = new long[8];
		for (int y = 0; y < 8; y++) {
			out[y] = ~layers[y];
		}
		return out;
	}

	/** A wall along the cell's west edge (x 0..1), floor to top. */
	private static long[] westWall() {
		long[] layers = new long[8];
		for (int y = 0; y < 8; y++) {
			for (int z = 0; z < 8; z++) {
				layers[y] |= 3L << (z * 8);
			}
		}
		return layers;
	}

	@Test
	void shouldSeeGeometryOnlyThroughTheMiddle() {
		assertFalse(FluidCells.middleBlocked(westWall(), 0.0, 1.0));
		assertTrue(FluidCells.middleBlocked(solid(2, 3), 0.0, 1.0));
		assertFalse(FluidCells.middleBlocked(solid(2, 3), 0.5, 1.0));
		assertFalse(FluidCells.middleBlocked(null, 0.0, 1.0));
	}

	@Test
	void shouldLetAFluidFallPastAWallInItsCell() {
		// water put by a wall in mid-air: nothing under its middle, an empty cell below
		assertNull(FluidCells.down(1.0F, 0.0F, westWall(), 0.0F, null, null));
	}

	@Test
	void shouldKeepAFluidOnItsFloor() {
		assertEquals("resting on ground", FluidCells.down(1.0F, 0.25F, solid(0, 1), 0.0F, solid(0, 7), null));
		// a floor right at the cell's bottom: its top layer is in the cell below
		assertEquals("resting on ground", FluidCells.down(1.0F, 0.0F, null, 0.95F, solid(0, 7), null));
		// a steep slope through the middle, no floor to walk on: still not through it
		assertEquals("resting on ground", FluidCells.down(1.0F, 0.0F, solid(2, 2), 0.0F, null, null));
	}

	@Test
	void shouldNotMindACeilingAboveItsSurface() {
		// a ceiling in the top layer of a shallow fluid's cell
		assertNull(FluidCells.down(0.5F, 0.0F, solid(7, 7), 0.0F, null, null));
	}

	@Test
	void shouldNotFallOntoGroundUpToWhereItWouldLand() {
		assertEquals("ground below", FluidCells.down(1.0F, 0.0F, null, 0.88F, solid(6, 6), null));
		assertNull(FluidCells.down(1.0F, 0.0F, null, 0.5F, solid(3, 3), null));
	}

	@Test
	void shouldFlowIntoACellAWallCrossesTheFarSideOf() {
		assertNull(FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, westWall(), null, null, true));
	}

	@Test
	void shouldNotFlowPastAWallToTheMiddle() {
		// a wall between the fluid and the middle, or a brush the middle is inside of
		assertEquals("wall beside", FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, westWall(), null, null, false));
	}

	@Test
	void shouldNotFlowUpAStepAboveItsSurface() {
		// a step of a third of a block, its top walkable, against a fluid of a ninth
		assertEquals("ground beside", FluidCells.sideways(1.0F / 9.0F, 0.0F, 0.375F, solid(0, 2), null, null, true));
	}

	@Test
	void shouldFlowOntoLevelGround() {
		assertNull(FluidCells.sideways(0.5F, 0.85F, 0.9F, solid(7, 7), null, null, true));
	}

	@Test
	void shouldNotFlowUnderTheGround() {
		assertEquals("under ground", FluidCells.sideways(0.8F, 0.0F, 0.0F, null, null, solid(0, 7), true));
		// a thick brush has only its surfaces: under its floor high in the cell above, the empty cell is inside it
		assertEquals("under ground", FluidCells.sideways(0.8F, 0.0F, 0.0F, null, null, solid(3, 3), true));
		assertNull(FluidCells.sideways(0.8F, 0.0F, 0.0F, null, null, null, true));
	}

	@Test
	void shouldNotFlowIntoTheVoidBeyondTheMap() {
		// a kept wall cell by a tunnel: a thin brush on the room's side, out of the way of the middle, the void
		// behind it there; nothing crosses the way to the middle, and its floor is the tunnel's
		long[] wall = westWall();
		assertEquals("void beside", FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, wall, not(wall), null, true));
		// a cell wholly void, no geometry at all
		assertEquals("void beside", FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, null, solid(0, 7), null, true));
		// dug, its void doesn't count (HostDig.voidLayers gives none)
		assertNull(FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, wall, null, null, true));
	}

	@Test
	void shouldFlowOverAFloorWithTheVoidUnderIt() {
		// a floor brush at 0.25..0.375 of the cell, the void under it: the fluid runs on the floor
		assertNull(FluidCells.sideways(0.8F, 0.375F, 0.375F, solid(2, 2), solid(0, 1), null, true));
	}

	@Test
	void shouldNotFallOntoTheVoid() {
		assertEquals("void below", FluidCells.down(1.0F, 0.0F, null, 0.0F, null, solid(0, 7)));
		// the void low in the cell below, under a floor there: it lands on that floor (its own check)
		assertNull(FluidCells.down(1.0F, 0.0F, null, 0.5F, solid(3, 3), solid(0, 2)));
	}
}
