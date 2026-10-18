package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class ForgottenBlocksTest {
	private static final long CELL = 0x1234_5678_9ABCL;
	private static final long OTHER = -42L;

	@Test
	void shouldHoldEveryCheckpointForABlockNeverForgotten() {
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(OTHER, 7);
		assertTrue(blocks.holds(CELL, 0));
		assertTrue(blocks.holds(CELL, 3));
	}

	@Test
	void shouldHoldOnlyTheCheckpointsWrittenAfterTheBlockWasForgotten() {
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(CELL, 3); // revealed after checkpoint 3
		assertFalse(blocks.holds(CELL, 0)); // from before checkpoints were numbered
		assertFalse(blocks.holds(CELL, 2));
		assertFalse(blocks.holds(CELL, 3));
		assertTrue(blocks.holds(CELL, 4));
	}

	@Test
	void shouldHoldNoCheckpointForABlockForgottenBeforeAnyWasWritten() {
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(CELL, 0);
		assertFalse(blocks.holds(CELL, 0));
		assertTrue(blocks.holds(CELL, 1));
	}

	@Test
	void shouldNeverPutBackWhatAnEarlierCheckpointHasOfAForgottenBlock() {
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(CELL, 3);
		// the air of the wall from before the reveal: the block stays as it is
		assertNull(blocks.wanted(CELL, 2, "air", null));
		// tracked again since (mined, say): back to how it was before that, the revealed stone
		assertEquals("stone", blocks.wanted(CELL, 2, "air", "stone"));
		// a checkpoint after the reveal has it as it was then
		assertEquals("torch", blocks.wanted(CELL, 4, "torch", "stone"));
		// a checkpoint without it: as before anything tracked changed it
		assertEquals("stone", blocks.wanted(CELL, 4, null, "stone"));
		// a block never forgotten is as the checkpoint has it
		assertEquals("air", blocks.wanted(OTHER, 0, "air", "dirt"));
	}

	@Test
	void shouldPruneTheBlocksNoKeptCheckpointCouldPutBack() {
		long third = 7L;
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(CELL, 3);
		blocks.forget(OTHER, 5);
		blocks.forget(third, 9);
		blocks.prune(0); // the oldest kept from before checkpoints were numbered: every one may still matter
		assertEquals(6, blocks.pairs().length);
		blocks.prune(6); // checkpoints 6 and on are left: 3 and 5 put nothing back over the blocks forgotten then
		assertEquals(2, blocks.pairs().length);
		assertFalse(blocks.holds(third, 9));
		assertTrue(blocks.holds(third, 10));
		blocks.prune(10);
		assertTrue(blocks.isEmpty());
	}

	@Test
	void shouldKeepABlockForgottenAtTheOldestKeptCheckpoint() {
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(CELL, 6); // forgotten after checkpoint 6, which is kept: it still has the block as it was
		blocks.prune(6);
		assertFalse(blocks.holds(CELL, 6));
	}

	@Test
	void shouldComeBackTheSameFromItsPairs() {
		ForgottenBlocks blocks = new ForgottenBlocks();
		blocks.forget(CELL, 3);
		blocks.forget(OTHER, 0);
		blocks.forget(CELL, 5); // again, later
		ForgottenBlocks copy = ForgottenBlocks.of(blocks.pairs());
		assertEquals(4, blocks.pairs().length);
		assertFalse(copy.holds(CELL, 5));
		assertTrue(copy.holds(CELL, 6));
		assertFalse(copy.holds(OTHER, 0));
		assertTrue(copy.holds(OTHER, 1));
		assertTrue(ForgottenBlocks.of(new long[0]).holds(CELL, 0));
	}
}
