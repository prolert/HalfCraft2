package dev.halfcraft.world;

import static dev.halfcraft.world.RevealRetry.Step.*;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntPredicate;
import org.junit.jupiter.api.Test;

class RevealRetryTest {
	/** A reveal's ticks, from its first, until it's done or {@code ticks} pass: the tick and step of each one that isn't a wait or a fast try. */
	private static List<String> run(int ticks, IntPredicate loadedAndDug, IntPredicate sidesKnown) {
		List<String> out = new ArrayList<>();
		int age = 0;
		for (int tick = 0; tick < ticks; tick++) {
			RevealRetry.Step step = RevealRetry.step(age, loadedAndDug.test(tick), loadedAndDug.test(tick), sidesKnown.test(tick));
			if (step != SLOW_WAIT && step != FAST_TRY) {
				out.add(tick + " " + step);
			}
			if (step == REVEAL_DONE || step == DROP) {
				break;
			}
			age = RevealRetry.older(age);
		}
		return out;
	}

	@Test
	void shouldRevealOnItsFirstTickWhenEverySideIsKnown() {
		assertEquals(List.of("0 REVEAL_DONE"), run(1000, tick -> true, tick -> true));
	}

	@Test
	void shouldTryEachTickUntilTheSidesAreKnown() {
		assertEquals(FAST_TRY, RevealRetry.step(0, true, true, false));
		assertEquals(List.of("37 REVEAL_DONE"), run(1000, tick -> true, tick -> tick >= 37));
	}

	@Test
	void shouldRevealTheKnownSidesAfterTheFastTriesThenTryEveryTwentyTicks() {
		// the regions come at tick 100: the 60th try reveals what's known, then every 20 ticks until one knows them all
		assertEquals(List.of("59 REVEAL_KEEP", "79 REVEAL_KEEP", "99 REVEAL_KEEP", "119 REVEAL_DONE"), run(1000, tick -> true, tick -> tick >= 100));
	}

	@Test
	void shouldKeepTryingWhileASideStaysUnknown() {
		List<String> steps = run(100_000, tick -> true, tick -> false);
		assertEquals(1 + (100_000 - 60) / 20, steps.size());
		assertEquals("99999 REVEAL_KEEP", steps.get(steps.size() - 1));
	}

	@Test
	void shouldDropOnItsNextTryWhenTheCellIsUnloadedOrNotDug() {
		assertEquals(DROP, RevealRetry.step(0, false, false, true));
		assertEquals(DROP, RevealRetry.step(0, true, false, true));
		assertEquals(DROP, RevealRetry.step(RevealRetry.FAST_TRIES - 1, false, false, false));
		// unloaded at tick 130, between the slow tries at 119 and 139
		assertEquals(List.of("59 REVEAL_KEEP", "79 REVEAL_KEEP", "99 REVEAL_KEEP", "119 REVEAL_KEEP", "139 DROP"), run(1000, tick -> tick < 130, tick -> false));
	}

	@Test
	void shouldStayYoungerThanTheFastTriesAndOneSlowWait() {
		int age = 0;
		for (int tick = 0; tick < 10_000; tick++) {
			age = RevealRetry.older(age);
			assertTrue(age >= 0 && age < RevealRetry.FAST_TRIES + RevealRetry.SLOW_TICKS, "age " + age);
		}
	}
}
