package dev.halfcraft.world;

import static dev.halfcraft.link.Proto.*;
import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class DigMaterialsTest {
	@Test
	void shouldNameTheBlockOfEachMaterial() {
		assertEquals("minecraft:stone", DigMaterials.blockId(DIG_STONE));
		assertEquals("minecraft:stone_bricks", DigMaterials.blockId(DIG_STONE_BRICKS));
		assertEquals("minecraft:iron_block", DigMaterials.blockId(DIG_IRON));
		assertEquals("minecraft:oak_planks", DigMaterials.blockId(DIG_PLANKS));
		assertEquals("minecraft:dirt", DigMaterials.blockId(DIG_DIRT));
		assertEquals("minecraft:glass", DigMaterials.blockId(DIG_GLASS));
	}

	@Test
	void shouldTakeStoneForNoneOrAnUnknownMaterial() {
		assertEquals("minecraft:stone", DigMaterials.blockId(DIG_NONE));
		assertEquals("minecraft:stone", DigMaterials.blockId(99));
	}

	@Test
	void shouldFindTheMaterialOfABlockBack() {
		for (int m = DIG_STONE; m <= DIG_GLASS; m++) {
			assertEquals(m, DigMaterials.material(DigMaterials.blockId(m)));
		}
		assertEquals(DIG_STONE, DigMaterials.material("minecraft:cobblestone"));
	}
}
