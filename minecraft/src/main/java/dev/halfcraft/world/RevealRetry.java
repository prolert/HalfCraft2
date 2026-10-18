package dev.halfcraft.world;

/**
 * When a reveal queued around a dug cell ({@link HostDig}) runs. It waits for Half-Life to send the
 * regions of the cell's six neighbours: each tick for {@link #FAST_TRIES} ticks, revealing at the last
 * of them anyway, around the sides it knows; then, while a side is still unknown, again every
 * {@link #SLOW_TICKS} ticks, until a reveal knows all six. Revealing again does no harm: what a reveal
 * revealed is dug and no longer air, which {@link DigRules} leaves. The reveal goes, with nothing done,
 * once the cell's chunk isn't loaded or the cell isn't dug. Pure.
 */
final class RevealRetry {
	/** How many ticks in a row a reveal tries first. */
	static final int FAST_TRIES = 60;
	/** The ticks between its tries after those. */
	static final int SLOW_TICKS = 20;
	// the age of the last fast try, and of each slow one a multiple of SLOW_TICKS after it
	private static final int LAST_FAST = FAST_TRIES - 1;

	/** What a queued reveal does on a tick. */
	enum Step {
		/** A side is unknown: it tries again the next tick. */
		FAST_TRY,
		/** No try on this tick. */
		SLOW_WAIT,
		/** Every side is known: it reveals, and it's done. */
		REVEAL_DONE,
		/** A side is still unknown: it reveals around the known ones and tries again {@link #SLOW_TICKS} later. */
		REVEAL_KEEP,
		/** The cell's chunk isn't loaded or the cell isn't dug: it's done, with nothing revealed. */
		DROP
	}

	private RevealRetry() {
	}

	/**
	 * What a reveal of age {@code age} ({@link #older} after each tick, from 0 on its first) does on this tick.
	 *
	 * @param loaded - the cell's chunk is loaded
	 * @param dug - the cell is dug
	 * @param sidesKnown - Half-Life has sent the regions of all six neighbours
	 */
	static Step step(int age, boolean loaded, boolean dug, boolean sidesKnown) {
		if (age > LAST_FAST && (age - LAST_FAST) % SLOW_TICKS != 0) {
			return Step.SLOW_WAIT;
		}
		if (!loaded || !dug) {
			return Step.DROP;
		}
		if (sidesKnown) {
			return Step.REVEAL_DONE;
		}
		return age < LAST_FAST ? Step.FAST_TRY : Step.REVEAL_KEEP;
	}

	/** A reveal's age a tick later. It stays below FAST_TRIES + SLOW_TICKS: the slow tries go round. */
	static int older(int age) {
		return age < LAST_FAST + SLOW_TICKS ? age + 1 : age + 1 - SLOW_TICKS;
	}
}
