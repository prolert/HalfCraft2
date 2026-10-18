package dev.halfcraft.world;

import static dev.halfcraft.link.Proto.DIG_GLASS;
import static dev.halfcraft.link.Proto.DIG_IRON;
import static dev.halfcraft.link.Proto.DIG_PLANKS;
import static dev.halfcraft.link.Proto.DIG_STONE;
import static dev.halfcraft.link.Proto.TRI_DIGGABLE;
import static dev.halfcraft.link.Proto.TRI_SKY;
import static dev.halfcraft.link.Proto.TRI_STAIR_HELPER;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class HostTriTest {
	private static final float[] WALL = { 0, 0, 0, 0, 1, 0, 0, 1, 1 };
	private static final int S = HostTri.MATERIAL_SHIFT;

	private static HostTri t(int flags) {
		return new HostTri(WALL, 0, flags);
	}

	@Test
	void shouldReadTheDigMaterialFromBits8To15() {
		HostTri tri = t(TRI_DIGGABLE | DIG_IRON << S);
		assertTrue(tri.diggable());
		assertEquals(DIG_IRON, tri.digMaterial);
	}

	@Test
	void shouldDigAsStoneWhenNoMaterialCame() {
		HostTri tri = t(TRI_DIGGABLE);
		assertTrue(tri.diggable());
		assertEquals(DIG_STONE, tri.digMaterial);
	}

	@Test
	void shouldPassAnUnknownMaterialThroughUnchanged() {
		HostTri tri = t(TRI_DIGGABLE | 200 << S);
		assertTrue(tri.diggable());
		assertEquals(200, tri.digMaterial);
	}

	@Test
	void shouldNotDigWithoutTheFlag() {
		HostTri glass = t(DIG_GLASS << S);
		assertFalse(glass.diggable());
		assertEquals(0, glass.digMaterial);
		assertFalse(t(TRI_SKY | TRI_STAIR_HELPER).diggable());
		assertFalse(new HostTri(WALL, 0, false).diggable());
	}

	@Test
	void shouldKeepTheStairHelperBesideTheDigFlags() {
		HostTri tri = t(TRI_STAIR_HELPER | TRI_DIGGABLE | DIG_PLANKS << S);
		assertTrue(tri.stairHelper);
		assertTrue(tri.diggable());
		assertEquals(DIG_PLANKS, tri.digMaterial);
	}
}
