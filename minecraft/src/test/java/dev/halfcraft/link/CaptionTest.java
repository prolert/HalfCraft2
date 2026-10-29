package dev.halfcraft.link;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;

class CaptionTest {
	private static final int WHITE = Caption.NO_COLOR;

	@Test
	void readsALineInItsSpeakersColour() {
		Caption caption = Caption.parse("0 - <clr:255,176,0>You made it! ");
		assertFalse(caption.sfx());
		assertEquals(List.of(new Caption.Phrase(0.0F, List.of(new Caption.Span("You made it!", 0xFFB000, false, false)))), caption.phrases());
	}

	@Test
	void readsASoundEffect() {
		Caption caption = Caption.parse("1 - <sfx><norepeat:5><playerclr:255,176,0:255,255,255>[Crowbar Swoosh]");
		assertTrue(caption.sfx());
		assertEquals("[Crowbar Swoosh]", caption.phrases().get(0).spans().get(0).text());
	}

	@Test
	void takesThePlayersColourForWhatThePlayerMade() {
		assertEquals(0xFFB000, Caption.parse("1 - <playerclr:255,176,0:255,255,255>[Pistol Shot]").phrases().get(0).spans().get(0).color());
		assertEquals(0xFFFFFF, Caption.parse("0 - <playerclr:255,176,0:255,255,255>[Pistol Shot]").phrases().get(0).spans().get(0).color());
	}

	@Test
	void togglesItalicsAndBold() {
		assertEquals(List.of(new Caption.Span("This is red", 0xFF0000, false, false), new Caption.Span(" and italics", 0xFF0000, true, false),
			new Caption.Span(" and\nbold", 0xFF0000, true, true), new Caption.Span(" white again.", 0xFFFFFF, false, false)),
			Caption.parse("0 - <clr:255,0,0>This is red<I> and italics<B> and<cr>bold<B><I><clr:255,255,255> white again.").phrases().get(0).spans());
	}

	@Test
	void splitsDelayedPhrases() {
		Caption caption = Caption.parse("0 - First.<delay:2.5>Second.");
		assertEquals(List.of(new Caption.Phrase(0.0F, List.of(new Caption.Span("First.", WHITE, false, false))),
			new Caption.Phrase(2.5F, List.of(new Caption.Span("Second.", WHITE, false, false)))), caption.phrases());
	}

	@Test
	void keepsAnAngleBracketThatOpensNoCommand() {
		assertEquals("a < b", Caption.parse("0 - a < b").phrases().get(0).spans().get(0).text());
	}

	@Test
	void readsWhoSaysIt() {
		Caption caption = Caption.parse("0 npc_citizen/odessa <clr:255,255,255>We've got a visitor!");
		assertEquals(new Caption.Speaker("npc_citizen", "odessa"), caption.speaker());
		assertEquals("We've got a visitor!", caption.phrases().get(0).spans().get(0).text());
	}

	@Test
	void readsNoSpeakerForADash() {
		assertNull(Caption.parse("0 - You made it!").speaker());
	}

	@Test
	void readsNothingFromWhatIsntACaption() {
		assertNull(Caption.parse("hello"));
		assertNull(Caption.parse(""));
		assertNull(Caption.parse("0 You made it!"));
		assertTrue(Caption.parse("0 -    ").phrases().isEmpty());
	}
}
