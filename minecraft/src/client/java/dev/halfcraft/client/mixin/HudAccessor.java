package dev.halfcraft.client.mixin;

import net.minecraft.client.gui.Hud;
import net.minecraft.client.gui.components.SubtitleOverlay;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(Hud.class)
public interface HudAccessor {
	/** Minecraft's subtitles, where Half-Life's sound captions go (HostCaptions). */
	@Accessor("subtitleOverlay")
	SubtitleOverlay halfcraft$subtitleOverlay();
}
