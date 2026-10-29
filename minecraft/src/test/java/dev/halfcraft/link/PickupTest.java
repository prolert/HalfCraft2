package dev.halfcraft.link;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import org.junit.jupiter.api.Test;

class PickupTest {
	@Test
	void readsAWeapon() {
		assertEquals(new Pickup.Weapon(Proto.HOST_WEAPON_PISTOL), Pickup.parse("weapon " + Proto.HOST_WEAPON_PISTOL));
	}

	@Test
	void readsAmmoWithItsCount() {
		assertEquals(new Pickup.Ammo("smg1_grenade", 3), Pickup.parse("ammo SMG1_Grenade 3"));
	}

	@Test
	void readsAnItem() {
		assertEquals(new Pickup.Item("item_healthkit"), Pickup.parse("item item_healthkit"));
	}

	@Test
	void readsNothingFromSomethingElse() {
		assertNull(Pickup.parse("ammo Pistol"));
		assertNull(Pickup.parse("ammo Pistol many"));
		assertNull(Pickup.parse("weapon pistol"));
		assertNull(Pickup.parse("armour item_battery"));
		assertNull(Pickup.parse(""));
	}
}
