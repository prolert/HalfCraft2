package dev.halfcraft.link;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;

class KeyHintTest {
	@Test
	void readsAKeyAndWhatItDoes() {
		assertEquals(List.of(new KeyHint.Row(new KeyHint.Binding("+jump"), "Jump")), KeyHint.parse("%+jump% JUMP").rows());
	}

	@Test
	void readsEveryRowOfAVehiclesKeys() {
		assertEquals(List.of(new KeyHint.Row(new KeyHint.Named("SHIFT"), "Turbo"), new KeyHint.Row(new KeyHint.Named("SPACE"), "Handbrake")),
			KeyHint.parse("%=SHIFT% TURBO %=SPACE% HANDBRAKE").rows());
	}

	@Test
	void readsAKeyByItsScancode() {
		assertEquals(List.of(new KeyHint.Row(new KeyHint.Scancode(10), "Open door")), KeyHint.parse("%sc:10% OPEN DOOR").rows());
	}

	@Test
	void writesTheTextAsASentenceInAnyScript() {
		assertEquals("Открыть дверь", KeyHint.parse("%sc:10% ОТКРЫТЬ ДВЕРЬ").rows().get(0).text());
	}

	@Test
	void joinsTheLinesOfAWrappedHint() {
		assertEquals("To swap gravity gun with previous gun", KeyHint.parse("%phys_swap% TO SWAP GRAVITY GUN\n WITH PREVIOUS GUN").rows().get(0).text());
	}

	@Test
	void keepsTextBeforeTheFirstKeyAsARowOfItsOwn() {
		assertEquals(List.of(new KeyHint.Row(null, "Forward"), new KeyHint.Row(new KeyHint.Binding("+forward"), "")),
			KeyHint.parse("FORWARD %+forward%").rows());
	}

	@Test
	void readsCommandsWhateverTheirCase() {
		assertEquals(new KeyHint.Binding("+use"), KeyHint.parse("%+USE% TO USE MOUNTED GUN").rows().get(0).key());
	}

	@Test
	void takesAnUnclosedPercentAsText() {
		assertEquals(List.of(new KeyHint.Row(new KeyHint.Binding("+attack"), "100% damage")), KeyHint.parse("%+attack% 100% DAMAGE").rows());
	}

	@Test
	void readsNothingFromAnEmptyHint() {
		assertTrue(KeyHint.parse("").rows().isEmpty());
	}
}
