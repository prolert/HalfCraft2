package dev.halfcraft.client.mixin;

import dev.halfcraft.client.HostDigClient;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.InteractionHand;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Attacking a diggable face of Half-Life's walls mines into it (HostDigClient) instead of Minecraft's
 * attack, which would take it for a miss: a miss cooldown (missTime) and no swing.
 */
@Mixin(Minecraft.class)
public abstract class MinecraftDigMixin {
	@Inject(method = "startAttack", at = @At("HEAD"), cancellable = true)
	private void halfcraft$digStart(CallbackInfoReturnable<Boolean> cir) {
		Minecraft self = (Minecraft) (Object) this;
		if (HostDigClient.attack(self)) {
			halfcraft$swing(self.player);
			cir.setReturnValue(true);
		}
	}

	@Inject(method = "continueAttack", at = @At("HEAD"), cancellable = true)
	private void halfcraft$digHold(boolean down, CallbackInfo ci) {
		Minecraft self = (Minecraft) (Object) this;
		if (down && self.player != null && !self.player.isUsingItem() && HostDigClient.attack(self)) {
			halfcraft$swing(self.player);
			ci.cancel();
		}
	}

	@Unique
	private static void halfcraft$swing(LocalPlayer player) {
		player.swing(InteractionHand.MAIN_HAND, player.getMainHandItem().getAttackAnimation(), false);
	}
}
