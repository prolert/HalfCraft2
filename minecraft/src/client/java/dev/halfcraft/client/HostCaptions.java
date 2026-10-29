package dev.halfcraft.client;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.client.mixin.HudAccessor;
import dev.halfcraft.link.Caption;
import dev.halfcraft.link.HostStrings;
import dev.halfcraft.link.Proto;
import java.util.ArrayList;
import java.util.List;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.client.resources.sounds.SimpleSoundInstance;
import net.minecraft.client.sounds.WeighedSoundEvents;
import net.minecraft.locale.Language;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.MutableComponent;
import net.minecraft.network.chat.Style;
import net.minecraft.network.chat.TextColor;
import net.minecraft.resources.Identifier;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.sounds.SoundSource;
import net.minecraft.util.RandomSource;
import net.minecraft.util.Util;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's close captions (STR_CAPTION), which it sends while Minecraft's Show Subtitles option
 * is on (MC_SUBTITLES) instead of showing its caption box (#26): speech goes into the chat as a
 * player's message would, {@code <Alyx> Come on, Gordon.}, in the speaker's colour; sounds go into
 * Minecraft's own subtitles. A speaker is named by their model first (halfcraft.speaker.model.*:
 * Odessa is a citizen) and their class then (halfcraft.speaker.*); one without a name here speaks
 * unnamed. A caption's delayed phrases follow at their time. Render thread only.
 */
public final class HostCaptions {
	private static final Identifier SOUND = Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, "caption");
	private static final SoundEvent SOUND_EVENT = SoundEvent.createVariableRangeEvent(SOUND);
	private static final float SUBTITLE_RANGE = 16.0F;

	private record Pending(long at, boolean sfx, Component text) {
	}

	private static final List<Pending> PENDING = new ArrayList<>();

	private HostCaptions() {
	}

	public static void init() {
		HostStrings.register(Proto.STR_CAPTION, HostCaptions::accept);
		ClientTickEvents.END_CLIENT_TICK.register(minecraft -> {
			if (!HostClient.linked()) {
				PENDING.clear();
				return;
			}
			long now = Util.getMillis();
			List<Pending> due = PENDING.stream().filter(p -> p.at() <= now).toList();
			PENDING.removeAll(due);
			due.forEach(p -> show(p.sfx(), p.text()));
		});
	}

	private static void accept(String text) {
		Caption caption = Caption.parse(text);
		if (caption == null) {
			HalfCraft.LOG.warn("HalfCraft: Half-Life sent a caption that isn't one: \"{}\"", text);
			return;
		}
		long now = Util.getMillis();
		Component name = caption.sfx() ? null : name(caption.speaker());
		for (Caption.Phrase phrase : caption.phrases()) {
			Component line = component(phrase.spans(), caption.sfx());
			if (name != null) {
				line = Component.translatable("chat.type.text", name, line);
			}
			if (phrase.delay() <= 0.0F) {
				show(caption.sfx(), line);
			} else {
				PENDING.add(new Pending(now + (long) (phrase.delay() * 1000.0F), caption.sfx(), line));
			}
		}
	}

	private static @Nullable Component name(Caption.@Nullable Speaker speaker) {
		if (speaker == null) {
			return null;
		}
		Language language = Language.getInstance();
		for (String key : List.of("halfcraft.speaker.model." + speaker.model(), "halfcraft.speaker." + speaker.classname())) {
			if (language.has(key)) {
				return Component.translatable(key);
			}
		}
		return null;
	}

	private static Component component(List<Caption.Span> spans, boolean sfx) {
		MutableComponent line = Component.empty();
		for (Caption.Span span : spans) {
			// a subtitle is one line
			String text = sfx ? span.text().replace('\n', ' ') : span.text();
			Style style = Style.EMPTY.withItalic(span.italic()).withBold(span.bold());
			if (span.color() != Caption.NO_COLOR) {
				style = style.withColor(TextColor.fromRgb(span.color()));
			}
			line.append(Component.literal(text).withStyle(style));
		}
		return line;
	}

	private static void show(boolean sfx, Component text) {
		Minecraft minecraft = Minecraft.getInstance();
		if (minecraft.player == null) {
			return;
		}
		if (!sfx) {
			minecraft.gui.hud.getChat().addClientSystemMessage(text);
			return;
		}
		// a sound Minecraft never plays, heard where the player stands: Half-Life doesn't say where it was
		var at = minecraft.player.position();
		var sound = new SimpleSoundInstance(SOUND_EVENT, SoundSource.MASTER, 1.0F, 1.0F, RandomSource.create(), at.x, at.y, at.z);
		var events = new WeighedSoundEvents(SOUND, "subtitles.halfcraft.caption") {
			@Override
			public Component getSubtitle() {
				return text;
			}
		};
		((HudAccessor) minecraft.gui.hud).halfcraft$subtitleOverlay().onPlaySound(sound, events, SUBTITLE_RANGE);
	}
}
