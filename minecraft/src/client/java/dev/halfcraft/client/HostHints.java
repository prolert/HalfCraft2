package dev.halfcraft.client;

import com.mojang.blaze3d.platform.InputConstants;
import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostStrings;
import dev.halfcraft.link.KeyHint;
import dev.halfcraft.link.Proto;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.ChatFormatting;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.gui.components.toasts.TutorialToast;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.MutableComponent;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's key hints ("%+use% OPEN DOOR", STR_HINT) as Minecraft's tutorial toasts, up until
 * Half-Life's own hint would have faded (#26). Keys Minecraft has are named from its own bindings,
 * so a rebound key reads right. A hint naming a key nobody here has (squad commands, the suit's
 * zoom, weapon slots: Minecraft's hotbar picks the weapon) isn't shown. Render thread only.
 */
public final class HostHints {
	// Half-Life's commands for keys Minecraft has
	private static final Map<String, Function<Options, KeyMapping>> BINDINGS = Map.ofEntries(Map.entry("+forward", o -> o.keyUp),
		Map.entry("+back", o -> o.keyDown), Map.entry("+moveleft", o -> o.keyLeft), Map.entry("+moveright", o -> o.keyRight),
		Map.entry("+jump", o -> o.keyJump), Map.entry("+duck", o -> o.keyShift), Map.entry("toggle_duck", o -> o.keyShift),
		Map.entry("+speed", o -> o.keySprint), Map.entry("+attack", o -> o.keyAttack), Map.entry("+attack2", o -> o.keyUse));
	private static @Nullable TutorialToast shown;

	private HostHints() {
	}

	public static void init() {
		HostStrings.register(Proto.STR_HINT, HostHints::show);
		ClientTickEvents.END_CLIENT_TICK.register(minecraft -> {
			if (shown != null && !HostClient.linked()) {
				hide();
			}
		});
	}

	private static void show(String text) {
		hide();
		if (text.isEmpty()) {
			return;
		}
		Minecraft minecraft = Minecraft.getInstance();
		List<Component> rows = new ArrayList<>();
		boolean mouse = false;
		for (KeyHint.Row row : KeyHint.parse(text).rows()) {
			Component key = null;
			if (row.key() != null) {
				key = switch (row.key()) {
					case KeyHint.Binding binding -> {
						Function<Options, KeyMapping> mapping = BINDINGS.get(binding.command());
						if (mapping == null) {
							yield null;
						}
						KeyMapping bound = mapping.apply(minecraft.options);
						mouse |= KeyMappingHelper.getBoundKeyOf(bound).getType() == InputConstants.Type.MOUSE;
						yield bound.getTranslatedKeyMessage();
					}
					case KeyHint.Scancode scancode -> InputConstants.Type.KEYBOARD.getOrCreate(scancode.scancode()).getDisplayName();
					case KeyHint.Named named -> Component.literal(named.name());
				};
				if (key == null) {
					HalfCraft.LOG.info("HalfCraft: Half-Life's hint \"{}\" names a key nobody has here ({}); not shown", text, row.key());
					return;
				}
			}
			MutableComponent line = Component.empty();
			if (key != null) {
				line.append(key.copy().withStyle(ChatFormatting.BOLD)).append(" ");
			}
			rows.add(line.append(row.text()));
		}
		if (rows.isEmpty()) {
			return;
		}
		MutableComponent message = null;
		for (Component row : rows.subList(1, rows.size())) {
			message = message == null ? row.copy() : message.append("\n").append(row);
		}
		shown = new TutorialToast(minecraft.font, mouse ? TutorialToast.Icons.MOUSE : TutorialToast.Icons.MOVEMENT_KEYS, rows.get(0), message, false);
		minecraft.gui.toastManager().addToast(shown);
	}

	private static void hide() {
		if (shown != null) {
			shown.hide();
			shown = null;
		}
	}
}
