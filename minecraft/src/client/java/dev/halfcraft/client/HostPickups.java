package dev.halfcraft.client;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostStrings;
import dev.halfcraft.link.Pickup;
import dev.halfcraft.link.Proto;
import dev.halfcraft.weapon.HostWeapon;
import dev.halfcraft.weapon.HostWeapons;
import java.util.Map;
import java.util.function.Supplier;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.toasts.Toast;
import net.minecraft.client.gui.components.toasts.ToastManager;
import net.minecraft.network.chat.Component;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.alchemy.PotionContents;
import net.minecraft.world.item.alchemy.Potions;

/**
 * What Half-Life's player picks up (STR_PICKUP), in a {@link PickupToast} instead of Half-Life's
 * pickup history (#26). Ammo shows the weapon that fires it; Half-Life's items show the Minecraft
 * item closest to what they do here (its armour is Minecraft's absorption, a golden apple's).
 * Render thread only.
 */
public final class HostPickups {
	private static final Map<String, HostWeapon> AMMO = Map.of("pistol", HostWeapon.PISTOL, "smg1", HostWeapon.SMG, "smg1_grenade", HostWeapon.SMG, "ar2",
		HostWeapon.PULSE_RIFLE, "ar2altfire", HostWeapon.PULSE_RIFLE, "357", HostWeapon.REVOLVER, "xbowbolt", HostWeapon.CROSSBOW, "buckshot",
		HostWeapon.SHOTGUN, "rpg_round", HostWeapon.RPG, "grenade", HostWeapon.GRENADE);
	private static final Map<String, Supplier<ItemStack>> ITEMS = Map.of("item_healthkit",
		() -> PotionContents.createItemStack(Items.POTION, Potions.HEALING), "item_healthvial",
		() -> PotionContents.createItemStack(Items.POTION, Potions.HEALING), "item_battery", () -> new ItemStack(Items.GOLDEN_APPLE),
		"item_grubnugget", () -> new ItemStack(Items.GLOW_BERRIES));

	private HostPickups() {
	}

	public static void init() {
		HostStrings.register(Proto.STR_PICKUP, HostPickups::show);
	}

	private static void show(String text) {
		Pickup pickup = Pickup.parse(text);
		PickupToast.Row row = pickup == null ? null : row(pickup);
		if (row == null) {
			HalfCraft.LOG.info("HalfCraft: Half-Life picked up \"{}\", which has no toast here", text);
			return;
		}
		int count = pickup instanceof Pickup.Ammo ammo ? ammo.count() : 1;
		ToastManager toasts = Minecraft.getInstance().gui.toastManager();
		PickupToast shown = toasts.getToast(PickupToast.class, Toast.NO_TOKEN);
		if (shown == null || !shown.add(row, count)) {
			PickupToast toast = new PickupToast();
			toast.add(row, count);
			toasts.addToast(toast);
		}
	}

	private static PickupToast.Row row(Pickup pickup) {
		return switch (pickup) {
			case Pickup.Weapon weapon -> {
				ItemStack icon = HostWeapons.standIn(weapon.weapon());
				yield icon.isEmpty() ? null : new PickupToast.Row("weapon " + weapon.weapon(), icon, n -> icon.getHoverName(), false);
			}
			case Pickup.Ammo ammo -> {
				HostWeapon weapon = AMMO.get(ammo.type());
				Component name = Component.translatable("halfcraft.ammo." + ammo.type());
				yield weapon == null ? null
					: new PickupToast.Row("ammo " + ammo.type(), HostWeapons.standIn(weapon.id()), n -> Component.translatable("halfcraft.pickup.count", n, name),
						true);
			}
			case Pickup.Item item -> {
				Supplier<ItemStack> icon = ITEMS.get(item.classname());
				Component name = Component.translatable("halfcraft.pickup." + item.classname());
				yield icon == null ? null
					: new PickupToast.Row("item " + item.classname(), icon.get(), n -> n > 1 ? Component.translatable("halfcraft.pickup.more", name, n) : name,
						true);
			}
		};
	}
}
