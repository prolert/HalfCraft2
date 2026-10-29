package dev.halfcraft.link;

import java.util.Locale;
import org.jspecify.annotations.Nullable;

/** Something Half-Life's player picked up (STR_PICKUP). */
public sealed interface Pickup permits Pickup.Weapon, Pickup.Ammo, Pickup.Item {
	/** @param weapon its WeaponId */
	record Weapon(int weapon) implements Pickup {
	}

	/** @param type Half-Life's ammo type, lower case ({@code pistol}, {@code smg1_grenade}, ...) */
	record Ammo(String type, int count) implements Pickup {
	}

	/** @param classname Half-Life's item ({@code item_healthkit}, {@code item_battery}, ...) */
	record Item(String classname) implements Pickup {
	}

	/** The pickup in {@code text}, or null when it isn't one. */
	static @Nullable Pickup parse(String text) {
		String[] parts = text.split(" ");
		try {
			return switch (parts[0]) {
				case "weapon" -> parts.length == 2 ? new Weapon(Integer.parseInt(parts[1])) : null;
				case "ammo" -> parts.length == 3 ? new Ammo(parts[1].toLowerCase(Locale.ROOT), Integer.parseInt(parts[2])) : null;
				case "item" -> parts.length == 2 ? new Item(parts[1].toLowerCase(Locale.ROOT)) : null;
				default -> null;
			};
		} catch (NumberFormatException e) {
			return null;
		}
	}
}
