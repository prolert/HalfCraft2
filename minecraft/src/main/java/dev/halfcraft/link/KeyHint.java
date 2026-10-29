package dev.halfcraft.link;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import org.jspecify.annotations.Nullable;

/**
 * One of Half-Life's key hints (STR_HINT) taken apart: rows of a key and what it does, in the order
 * Half-Life lists them ({@code "%+speed% TURBO %+jump% HANDBRAKE"}). Half-Life writes the text in
 * capitals; a row's text is a sentence here, the way Minecraft's own tutorial toasts write theirs.
 */
public record KeyHint(List<Row> rows) {
	/** A key as Half-Life refers to it. */
	public sealed interface Key permits Binding, Scancode, Named {
	}

	/** Whatever Minecraft binds to that Half-Life command ({@code +jump}, {@code +attack}, ...), lower case. */
	public record Binding(String command) implements Key {
	}

	/** A key by its SDL scancode: one Half-Life keeps while Minecraft has the keyboard. */
	public record Scancode(int scancode) implements Key {
	}

	/** A key Half-Life named itself (its own binding, while it has the keyboard). */
	public record Named(String name) implements Key {
	}

	/** @param key null for text before the hint's first key */
	public record Row(@Nullable Key key, String text) {
	}

	public static KeyHint parse(String hint) {
		List<Row> rows = new ArrayList<>();
		Key key = null;
		StringBuilder text = new StringBuilder();
		int at = 0;
		while (at < hint.length()) {
			int open = hint.indexOf('%', at);
			int close = open < 0 ? -1 : hint.indexOf('%', open + 1);
			if (close < 0) {
				text.append(hint, at, hint.length());
				break;
			}
			text.append(hint, at, open);
			if (key != null || !text.toString().isBlank()) {
				rows.add(new Row(key, sentence(text.toString())));
			}
			key = key(hint.substring(open + 1, close));
			text.setLength(0);
			at = close + 1;
		}
		if (key != null || !text.toString().isBlank()) {
			rows.add(new Row(key, sentence(text.toString())));
		}
		return new KeyHint(List.copyOf(rows));
	}

	private static Key key(String placeholder) {
		if (placeholder.startsWith("=")) {
			return new Named(placeholder.substring(1));
		}
		if (placeholder.startsWith("sc:")) {
			try {
				return new Scancode(Integer.parseInt(placeholder.substring(3)));
			} catch (NumberFormatException e) {
				return new Binding(placeholder.toLowerCase(Locale.ROOT));
			}
		}
		return new Binding(placeholder.toLowerCase(Locale.ROOT));
	}

	/** "OPEN DOOR" -> "Open door": one line, its spaces collapsed. */
	private static String sentence(String text) {
		// a line break comes as one or as the strings file's escape for one
		String line = text.replace("\\n", " ").strip().replaceAll("\\s+", " ").toLowerCase(Locale.ROOT);
		return line.isEmpty() ? line : line.substring(0, 1).toUpperCase(Locale.ROOT) + line.substring(1);
	}
}
