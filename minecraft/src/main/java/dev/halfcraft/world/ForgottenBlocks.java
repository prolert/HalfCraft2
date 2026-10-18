package dev.halfcraft.world;

import it.unimi.dsi.fastutil.longs.Long2LongOpenHashMap;
import org.jspecify.annotations.Nullable;

/**
 * The blocks Rollback stopped tracking ({@link Rollback#forget}), each with the number of the last
 * checkpoint written before it did. Checkpoints are numbered as they're written, from 1 (0: one from
 * before they were); one numbered that or lower has the block as it was before it was forgotten, which
 * no longer holds. Pure state.
 */
final class ForgottenBlocks {
	// block (BlockPos.asLong) -> the last checkpoint's number when it was forgotten
	private final Long2LongOpenHashMap forgotten = new Long2LongOpenHashMap();

	/** The block at key is forgotten now, with {@code checkpoints} the last checkpoint's number. */
	void forget(long key, long checkpoints) {
		this.forgotten.put(key, checkpoints);
	}

	/** Whether checkpoint number {@code checkpoint} has the block at key as it is to be: it was written after the block was last forgotten, or it never was. */
	boolean holds(long key, long checkpoint) {
		return !this.forgotten.containsKey(key) || checkpoint > this.forgotten.get(key);
	}

	boolean isEmpty() {
		return this.forgotten.isEmpty();
	}

	/**
	 * Drops the blocks no checkpoint numbered {@code oldestKept} or later has from before they were
	 * forgotten: those forgotten before it was written. For when the older checkpoints are gone.
	 */
	void prune(long oldestKept) {
		this.forgotten.long2LongEntrySet().removeIf(entry -> entry.getLongValue() < oldestKept);
	}

	/**
	 * What going back to checkpoint number {@code checkpoint} puts at key: what the checkpoint has there
	 * while that {@link #holds}, else what was there before anything tracked changed it (null: nothing to
	 * put back, the block stays as it is).
	 */
	<T> @Nullable T wanted(long key, long checkpoint, @Nullable T inCheckpoint, @Nullable T original) {
		return inCheckpoint != null && holds(key, checkpoint) ? inCheckpoint : original;
	}

	/** Each block's key and the number it was forgotten at, one after another. */
	long[] pairs() {
		long[] out = new long[this.forgotten.size() * 2];
		int i = 0;
		for (var entry : this.forgotten.long2LongEntrySet()) {
			out[i++] = entry.getLongKey();
			out[i++] = entry.getLongValue();
		}
		return out;
	}

	/** One from what {@link #pairs} gave. */
	static ForgottenBlocks of(long[] pairs) {
		ForgottenBlocks blocks = new ForgottenBlocks();
		for (int i = 0; i + 1 < pairs.length; i += 2) {
			blocks.forgotten.put(pairs[i], pairs[i + 1]);
		}
		return blocks;
	}
}
