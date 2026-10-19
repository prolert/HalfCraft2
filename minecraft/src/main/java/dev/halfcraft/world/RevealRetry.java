package dev.halfcraft.world;

/**
 * When a reveal queued around a dug cell ({@link HostDig}) runs. It waits for Half-Life to send the
 * regions of the cells the reveal reads ({@link DigRules#allKnown}: the cell's six neighbours and theirs,
 * which say whether a neighbour has a face Half-Life draws): it tries each tick for {@link #FAST_TRIES}
 * ticks, revealing at the last of them anyway, around the sides it knows; then, while a cell is still
 * unknown, every {@link #SLOW_TICKS} ticks, until a reveal knows them all. A later try reveals only what
 * the ones before left: what they revealed is dug, which {@link DigRules} leaves, while a side they left
 * (unknown then, next to an unknown cell, or with a Minecraft block in it since gone) is revealed if it's
 * still rock. The reveal goes, with nothing done, once the cell's chunk isn't loaded or the cell isn't
 * dug. Pure.
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
		/** A cell it reads is unknown: it tries again the next tick. */
		FAST_TRY,
		/** No try on this tick. */
		SLOW_WAIT,
		/** Every cell it reads is known: it reveals, and it's done. */
		REVEAL_DONE,
		/** A cell it reads is still unknown: it reveals around the known ones and tries again {@link #SLOW_TICKS} later. */
		REVEAL_KEEP,
		/** The cell's chunk isn't loaded or the cell isn't dug: it's done, with nothing revealed. */
		DROP
	}

	private RevealRetry() {
	}

	/** Whether a reveal of age {@code age} tries on this tick: only then is {@link #step} more than a wait. */
	static boolean tries(int age) {
		return age <= LAST_FAST || (age - LAST_FAST) % SLOW_TICKS == 0;
	}

	/**
	 * What a reveal of age {@code age} ({@link #older} after each tick, from 0 on its first) does on this tick.
	 *
	 * @param loaded - the cell's chunk is loaded
	 * @param dug - the cell is dug
	 * @param allKnown - Half-Life has sent the regions of every cell the reveal reads ({@link DigRules#allKnown})
	 */
	static Step step(int age, boolean loaded, boolean dug, boolean allKnown) {
		if (!tries(age)) {
			return Step.SLOW_WAIT;
		}
		if (!loaded || !dug) {
			return Step.DROP;
		}
		if (allKnown) {
			return Step.REVEAL_DONE;
		}
		return age < LAST_FAST ? Step.FAST_TRY : Step.REVEAL_KEEP;
	}

	/** A reveal's age a tick later. It stays below FAST_TRIES + SLOW_TICKS: the slow tries go round. */
	static int older(int age) {
		return age < LAST_FAST + SLOW_TICKS ? age + 1 : age + 1 - SLOW_TICKS;
	}
}
