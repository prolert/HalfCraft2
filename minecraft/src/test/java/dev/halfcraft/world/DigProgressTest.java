package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class DigProgressTest {
	private static final long CELL = 7L;
	private static final long OTHER = 8L;

	@Test
	void shouldTakeOneHitPerTick() {
		DigProgress progress = new DigProgress();
		assertEquals(DigProgress.Step.KNOCK, progress.hit(CELL, 1, 0.25F));
		// a click and a held button in the same tick
		assertEquals(DigProgress.Step.NONE, progress.hit(CELL, 1, 0.25F));
		assertEquals(2, progress.stage());
	}

	@Test
	void shouldOpenWhenProgressReachesOne() {
		DigProgress progress = new DigProgress();
		assertEquals(DigProgress.Step.KNOCK, progress.hit(CELL, 1, 0.25F));
		assertEquals(DigProgress.Step.MINE, progress.hit(CELL, 2, 0.25F));
		assertEquals(DigProgress.Step.MINE, progress.hit(CELL, 3, 0.25F));
		assertEquals(DigProgress.Step.OPEN, progress.hit(CELL, 4, 0.25F));
		assertFalse(progress.mining());
		assertEquals(-1, progress.stage());
	}

	@Test
	void shouldStartOverOnAnotherCell() {
		DigProgress progress = new DigProgress();
		progress.hit(CELL, 1, 0.5F);
		assertEquals(DigProgress.Step.KNOCK, progress.hit(OTHER, 2, 0.25F));
		assertEquals(OTHER, progress.cell());
		assertEquals(2, progress.stage());
	}

	@Test
	void shouldKeepProgressOverAPauseOfTwoTicks() {
		DigProgress progress = new DigProgress();
		progress.hit(CELL, 1, 0.5F);
		assertEquals(DigProgress.Step.MINE, progress.hit(CELL, 3, 0.25F));
		assertEquals(7, progress.stage());
	}

	@Test
	void shouldStartOverAfterALongerPause() {
		DigProgress progress = new DigProgress();
		progress.hit(CELL, 1, 0.5F);
		assertEquals(DigProgress.Step.KNOCK, progress.hit(CELL, 4, 0.25F));
		assertEquals(2, progress.stage());
	}

	@Test
	void shouldDropTheCrackWhenHitsStop() {
		DigProgress progress = new DigProgress();
		progress.hit(CELL, 1, 0.5F);
		progress.tick(3);
		assertTrue(progress.mining());
		assertEquals(5, progress.stage());
		progress.tick(4);
		assertFalse(progress.mining());
		assertEquals(-1, progress.stage());
	}

	@Test
	void shouldWaitFiveTicksAfterOpening() {
		DigProgress progress = new DigProgress();
		assertEquals(DigProgress.Step.OPEN, progress.hit(CELL, 1, 1.0F));
		assertEquals(DigProgress.Step.NONE, progress.hit(OTHER, 6, 1.0F));
		assertEquals(DigProgress.Step.OPEN, progress.hit(OTHER, 7, 1.0F));
	}

	@Test
	void shouldKnockOnEveryFourthHit() {
		DigProgress progress = new DigProgress();
		for (int t = 1; t <= 9; t++) {
			assertEquals(t % 4 == 1 ? DigProgress.Step.KNOCK : DigProgress.Step.MINE, progress.hit(CELL, t, 0.01F), "tick " + t);
		}
	}
}
