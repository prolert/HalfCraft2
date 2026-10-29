package dev.halfcraft.mixin;

import dev.halfcraft.weapon.HostWeapons;
import net.minecraft.network.protocol.game.ServerboundContainerClosePacket;
import net.minecraft.network.protocol.game.ServerboundSetCreativeModeSlotPacket;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.world.inventory.Slot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * The creative inventory moves items on a cursor of its own and tells the server only what each
 * slot holds afterwards: a stand-in picked up there was missing for the next tick, which put it back
 * where it was and took the one put down on the hotbar for a second copy (#30). HostWeapons follows
 * that cursor from the slots it changes, and forgets it when the screen closes.
 */
@Mixin(ServerGamePacketListenerImpl.class)
public abstract class WeaponCreativeMixin {
	@Shadow
	public ServerPlayer player;

	@Inject(
		method = "handleSetCreativeModeSlot",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/inventory/Slot;setByPlayer(Lnet/minecraft/world/item/ItemStack;)V")
	)
	private void halfcraft$followCreativeCursor(ServerboundSetCreativeModeSlotPacket packet, CallbackInfo ci) {
		Slot slot = player.inventoryMenu.getSlot(packet.slotNum());
		HostWeapons.creativeSlotSet(player, slot.getItem(), packet.itemStack());
	}

	@Inject(method = "handleContainerClose", at = @At("TAIL"))
	private void halfcraft$forgetCreativeCursor(ServerboundContainerClosePacket packet, CallbackInfo ci) {
		HostWeapons.screenClosed(player);
	}
}
