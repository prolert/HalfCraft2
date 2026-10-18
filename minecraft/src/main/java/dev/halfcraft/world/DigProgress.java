package dev.halfcraft.world;

/**
 * Mining one cell of Half-Life's walls, hit by hit, as Minecraft mines a block: each tick's hit adds
 * how much of the cell's block it breaks, and the cell opens once that reaches the whole block.
 * Hitting another cell, or none for more than {@link #STALE_TICKS} ticks, starts over; after a cell
 * opens, the next waits {@link #DELAY_TICKS} ticks (Minecraft's destroy delay). A cell is
 * {@code BlockPos.asLong}. Pure state, no Minecraft.
 */
public final class DigProgress {
	/** What a hit does. */
	public enum Step {
		/** Nothing: a second hit in the same tick, or one in the delay after an opening. */
		NONE,
		/** Mining on. */
		MINE,
		/** Mining on, with the block's hit sound and a particle (every fourth hit, the first included). */
		KNOCK,
		/** The cell is mined through: it opens. */
		OPEN
	}

	/** How many ticks without a hit the mining keeps its progress. */
	public static final int STALE_TICKS = 2;
	/** How many ticks after an opening go by before the next hit counts. */
	public static final int DELAY_TICKS = 5;

	private boolean mining;
	private long cell;
	private float progress;
	private int hits;
	private int lastTick;
	// the first tick a hit counts on
	private int readyAt = Integer.MIN_VALUE;

	/** A hit on {@code cell} on tick {@code tick}, breaking {@code delta} of its block (1 for the whole block). */
	public Step hit(long cell, int tick, float delta) {
		if (tick < this.readyAt || this.mining && this.cell == cell && this.lastTick == tick) {
			return Step.NONE;
		}
		if (!this.mining || this.cell != cell || tick - this.lastTick > STALE_TICKS) {
			this.mining = true;
			this.cell = cell;
			this.progress = 0.0F;
			this.hits = 0;
		}
		this.progress += delta;
		this.hits++;
		this.lastTick = tick;
		if (this.progress >= 1.0F) {
			clear();
			// as Minecraft's destroyDelay of 5: the next hit on tick + 6
			this.readyAt = tick + DELAY_TICKS + 1;
			return Step.OPEN;
		}
		return (this.hits - 1) % 4 == 0 ? Step.KNOCK : Step.MINE;
	}

	/** Each tick: mining that went without a hit for longer than {@link #STALE_TICKS} stops. */
	public void tick(int tick) {
		if (this.mining && tick - this.lastTick > STALE_TICKS) {
			clear();
		}
	}

	public boolean mining() {
		return this.mining;
	}

	/** The cell being mined, or the one mined last. */
	public long cell() {
		return this.cell;
	}

	/** The crack's stage, 0-9, or -1 for none. */
	public int stage() {
		return this.mining && this.progress > 0.0F ? Math.min(9, (int) (this.progress * 10.0F)) : -1;
	}

	/** Stops mining, and any delay after an opening. */
	public void clear() {
		this.mining = false;
		this.progress = 0.0F;
		this.hits = 0;
		this.readyAt = Integer.MIN_VALUE;
	}
}
