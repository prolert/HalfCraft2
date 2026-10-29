package dev.halfcraft.link;

import java.util.ArrayList;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * One of Half-Life's close captions (STR_CAPTION) taken apart: what Half-Life's caption box would
 * show, with its markup ({@code <clr:r,g,b>}, {@code <I>}, {@code <B>}, {@code <cr>},
 * {@code <playerclr:...>}) as styled spans, split where {@code <delay:seconds>} holds the rest
 * back. {@code <sfx>} marks a sound rather than speech; the rest of Half-Life's commands only
 * time its box and mean nothing here. Its speaker is who Half-Life played it from, when it says.
 */
public record Caption(@Nullable Speaker speaker, boolean sfx, List<Phrase> phrases) {
	/** A span's colour when the caption sets none. */
	public static final int NO_COLOR = -1;

	/** Who says it: Half-Life's classname and model name, both lower case. */
	public record Speaker(String classname, String model) {
	}

	/** @param delay seconds after the caption starts */
	public record Phrase(float delay, List<Span> spans) {
	}

	/** @param color 0xRRGGBB, or {@link #NO_COLOR} */
	public record Span(String text, int color, boolean italic, boolean bold) {
	}

	/**
	 * {@code "<from player 0|1> <classname>/<model> <caption>"} ({@code -} for no speaker), or null
	 * when {@code text} isn't one.
	 */
	public static @Nullable Caption parse(String text) {
		if (text.length() < 2 || text.charAt(1) != ' ' || (text.charAt(0) != '0' && text.charAt(0) != '1')) {
			return null;
		}
		int end = text.indexOf(' ', 2);
		if (end < 0) {
			return null;
		}
		String who = text.substring(2, end);
		int slash = who.indexOf('/');
		if (!who.equals("-") && slash <= 0) {
			return null;
		}
		Speaker speaker = who.equals("-") ? null : new Speaker(who.substring(0, slash), who.substring(slash + 1));
		return new Reader(text.charAt(0) == '1').read(speaker, text.substring(end + 1));
	}

	private static final class Reader {
		private final boolean fromPlayer;
		private final List<Phrase> phrases = new ArrayList<>();
		private List<Span> spans = new ArrayList<>();
		private final StringBuilder span = new StringBuilder();
		private float delay;
		private int color = NO_COLOR;
		private boolean italic;
		private boolean bold;
		private boolean sfx;

		Reader(boolean fromPlayer) {
			this.fromPlayer = fromPlayer;
		}

		Caption read(@Nullable Speaker speaker, String stream) {
			int at = 0;
			while (at < stream.length()) {
				int close = stream.charAt(at) == '<' ? stream.indexOf('>', at) : -1;
				if (close < 0 || !command(stream.substring(at + 1, close))) {
					this.span.append(stream.charAt(at++));
					continue;
				}
				at = close + 1;
			}
			endPhrase();
			return new Caption(speaker, this.sfx, List.copyOf(this.phrases));
		}

		/** @return false for text that only looks like a command (no name) */
		private boolean command(String command) {
			int colon = command.indexOf(':');
			String name = colon < 0 ? command : command.substring(0, colon);
			String args = colon < 0 ? "" : command.substring(colon + 1);
			if (name.isEmpty() || name.chars().anyMatch(c -> !Character.isLetter(c))) {
				return false;
			}
			switch (name) {
				case "clr" -> style(() -> this.color = rgb(args, this.color));
				case "playerclr" -> {
					String[] both = args.split(":");
					if (both.length == 2) {
						style(() -> this.color = rgb(this.fromPlayer ? both[0] : both[1], this.color));
					}
				}
				case "I" -> style(() -> this.italic = !this.italic);
				case "B" -> style(() -> this.bold = !this.bold);
				case "cr" -> this.span.append('\n');
				case "sfx" -> this.sfx = true;
				case "delay" -> {
					endPhrase();
					try {
						this.delay = Math.max(0.0F, Float.parseFloat(args));
					} catch (NumberFormatException e) {
						// a delay that can't be read shows its phrase at once, as Half-Life's box does
					}
				}
				default -> {
				}
			}
			return true;
		}

		private void style(Runnable change) {
			endSpan();
			change.run();
		}

		private void endSpan() {
			if (!this.span.isEmpty()) {
				this.spans.add(new Span(this.span.toString(), this.color, this.italic, this.bold));
				this.span.setLength(0);
			}
		}

		private void endPhrase() {
			endSpan();
			List<Span> trimmed = trim(this.spans);
			if (!trimmed.isEmpty()) {
				this.phrases.add(new Phrase(this.delay, trimmed));
			}
			this.spans = new ArrayList<>();
		}

		/** The phrase without the spaces at its ends, and without spans left empty by that. */
		private static List<Span> trim(List<Span> spans) {
			List<Span> out = new ArrayList<>(spans);
			while (!out.isEmpty() && out.getFirst().text().isBlank()) {
				out.removeFirst();
			}
			while (!out.isEmpty() && out.getLast().text().isBlank()) {
				out.removeLast();
			}
			if (out.isEmpty()) {
				return out;
			}
			Span first = out.getFirst();
			out.set(0, new Span(first.text().stripLeading(), first.color(), first.italic(), first.bold()));
			Span last = out.getLast();
			out.set(out.size() - 1, new Span(last.text().stripTrailing(), last.color(), last.italic(), last.bold()));
			return out;
		}

		private static int rgb(String args, int fallback) {
			String[] parts = args.split(",");
			if (parts.length != 3) {
				return fallback;
			}
			try {
				int rgb = 0;
				for (String part : parts) {
					rgb = rgb << 8 | Math.clamp(Integer.parseInt(part.strip()), 0, 255);
				}
				return rgb;
			} catch (NumberFormatException e) {
				return fallback;
			}
		}
	}
}
