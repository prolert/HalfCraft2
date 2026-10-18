package dev.halfcraft.world;

import static dev.halfcraft.link.Proto.*;

import java.util.ArrayList;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * Which cells around a dug one become blocks (are revealed): those wholly rock to a tunnel (Half-Life's
 * brushes and its void beyond the map, joined) that nothing dug or built yet, and not the map's sky.
 * Revealed, a cell is dug and holds the block; a cell only partly rock keeps Half-Life's surface, and
 * the hole's walls show the rest. Rock mostly void (behind the map) is stone; inside a thick wall it is
 * the wall's material.
 */
public final class DigRules {
	// sub-voxels of a cell (8 layers of 64)
	private static final int SUB_VOXELS = 8 * 64;
	// down, up, north, south, west, east: Minecraft's Direction order
	private static final int[][] SIDES = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };

	/** The world around a dug cell, as the rules read it. */
	public interface Cells {
		/** Half-Life has sent the cell's region. */
		boolean known(int x, int y, int z);

		/** All of the cell's Half-Life geometry is the map's sky (its skybox brushes). */
		boolean sky(int x, int y, int z);

		/** The cell is dug already (or out of reach: left alone). */
		boolean dug(int x, int y, int z);

		/** Minecraft has nothing in the cell. */
		boolean air(int x, int y, int z);

		/** The cell's Half-Life geometry as 8 layers (bit z * 8 + x of layer y), or null for none. */
		long @Nullable [] occupancy(int x, int y, int z);

		/** The cell's void beyond the map as 8 layers, or null for none. */
		long @Nullable [] voids(int x, int y, int z);
	}

	/** A cell that becomes a block of a Proto.DIG_* material. */
	public record Reveal(int x, int y, int z, int material) {
	}

	private DigRules() {
	}

	/**
	 * The cells around the dug cell (x, y, z) that are revealed, its {@code material} opened. A neighbour
	 * is revealed when, checked in this order, its region is known, it isn't the map's sky (the sky's
	 * brushes are rock to {@link #whollyRock}, and a block in one would hang against the skybox), it is
	 * wholly rock, it isn't dug and Minecraft has nothing in it.
	 */
	public static List<Reveal> reveals(Cells cells, int x, int y, int z, int material) {
		List<Reveal> out = new ArrayList<>();
		for (int[] side : SIDES) {
			int nx = x + side[0], ny = y + side[1], nz = z + side[2];
			if (!cells.known(nx, ny, nz) || cells.sky(nx, ny, nz)) {
				continue;
			}
			long[] voids = cells.voids(nx, ny, nz);
			if (whollyRock(cells.occupancy(nx, ny, nz), voids) && !cells.dug(nx, ny, nz) && cells.air(nx, ny, nz)) {
				out.add(new Reveal(nx, ny, nz, revealMaterial(voids, material)));
			}
		}
		return out;
	}

	/** Every sub-voxel is rock: in Half-Life's geometry or in its void. */
	public static boolean whollyRock(long @Nullable [] occupancy, long @Nullable [] voids) {
		for (long layer : HostCollision.rock(occupancy, voids)) {
			if (layer != -1L) {
				return false;
			}
		}
		return true;
	}

	/**
	 * What a revealed cell is: stone when at least half of it is void (behind the map), else the
	 * material opened next to it; stone for DIG_NONE or a material it doesn't know.
	 */
	public static int revealMaterial(long @Nullable [] voids, int material) {
		if (voidCount(voids) >= SUB_VOXELS / 2 || material < DIG_STONE || material > DIG_GLASS) {
			return DIG_STONE;
		}
		return material;
	}

	private static int voidCount(long @Nullable [] voids) {
		int count = 0;
		if (voids != null) {
			for (long layer : voids) {
				count += Long.bitCount(layer);
			}
		}
		return count;
	}
}
