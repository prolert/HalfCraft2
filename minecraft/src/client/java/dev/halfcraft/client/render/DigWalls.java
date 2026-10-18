package dev.halfcraft.client.render;

import org.jspecify.annotations.Nullable;

/**
 * The walls of holes dug into Half-Life's walls (from SkyCraft's DigWalls, by rock alone). Where a dug
 * cell meets a neighbour that isn't dug, isn't filled by a block that hides what's behind it and is
 * partly rock (Half-Life's geometry or its void beyond the map), the neighbour's slice of sub-voxels
 * against the face is drawn on it, an eighth of a block at a time, in the texture of the neighbour's
 * material: the hole is closed all round and nothing shows through Half-Life's cut walls.
 */
final class DigWalls {
	// by Direction ordinal: down, up, north, south, west, east
	private static final int[][] STEP = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };

	/** The world around the dug cells, as the walls read it. */
	interface Cells {
		/** The cell is dug. */
		boolean dug(int x, int y, int z);

		/** A block that hides what's behind it fills the cell. */
		boolean occludes(int x, int y, int z);

		/** The cell's rock as 8 layers (bit z * 8 + x of layer y), or null while it isn't known. Never changed. */
		long @Nullable [] rock(int x, int y, int z);

		/** The Proto.DIG_* material the cell's rock shows. */
		int material(int x, int y, int z);
	}

	interface Out {
		/**
		 * One quad of a wall: four corners (x, y, z each, section-relative) facing {@code face} (a Direction
		 * ordinal), and their uvs (u, v each, 0-1 across the face of a block of the Proto.DIG_* {@code material},
		 * v down). The arrays are used again for the next quad.
		 */
		void quad(float[] corners, int face, int material, float[] uv);
	}

	private DigWalls() {
	}

	/** Adds the walls around the dug cells of section (sx, sy, sz) ({@code dug}: DugBits, left as they are); returns how many quads. */
	static int add(int sx, int sy, int sz, long[] dug, Cells cells, Out out) {
		Walls walls = new Walls(sx, sy, sz, cells, out);
		for (int word = 0; word < dug.length; word++) {
			long bits = dug[word];
			while (bits != 0) {
				int bit = word * 64 + Long.numberOfTrailingZeros(bits);
				bits &= bits - 1;
				int x = sx * 16 + (bit & 15), y = sy * 16 + (bit >> 8), z = sz * 16 + (bit >> 4 & 15);
				if (cells.occludes(x, y, z)) {
					continue; // a block fills the dug cell: nothing to see from in it
				}
				for (int d = 0; d < STEP.length; d++) {
					walls.face(x, y, z, d);
				}
			}
		}
		return walls.quads;
	}

	/** A point on the face toward d, (u, v) across it from the dug cell's min corner: u right and v up seen from inside. */
	private static void frame(int d, double u, double v, double[] p) {
		switch (d) {
			case 0 -> set(p, u, 0, 1 - v);
			case 1 -> set(p, u, 1, v);
			case 2 -> set(p, u, v, 0);
			case 3 -> set(p, 1 - u, v, 1);
			case 4 -> set(p, 0, v, 1 - u);
			default -> set(p, 1, v, u);
		}
	}

	private static void set(double[] p, double x, double y, double z) {
		p[0] = x;
		p[1] = y;
		p[2] = z;
	}

	private static final class Walls {
		final int sx, sy, sz;
		final Cells cells;
		final Out out;
		int quads;
		private final double[] p = new double[3];
		private final float[] corners = new float[12];
		private final float[] uv = new float[8];

		Walls(int sx, int sy, int sz, Cells cells, Out out) {
			this.sx = sx;
			this.sy = sy;
			this.sz = sz;
			this.cells = cells;
			this.out = out;
		}

		/** The wall between the dug cell (x, y, z) and its neighbour toward d: a quad for each run of rock along a row. */
		void face(int x, int y, int z, int d) {
			int nx = x + STEP[d][0], ny = y + STEP[d][1], nz = z + STEP[d][2];
			if (this.cells.dug(nx, ny, nz) || this.cells.occludes(nx, ny, nz)) {
				return;
			}
			long[] rock = this.cells.rock(nx, ny, nz);
			if (rock == null) {
				return;
			}
			int material = 0;
			boolean any = false;
			for (int j = 0; j < 8; j++) {
				int row = 0;
				for (int i = 0; i < 8; i++) {
					if (this.occupied(rock, d, i, j)) {
						row |= 1 << i;
					}
				}
				while (row != 0) {
					int i0 = Integer.numberOfTrailingZeros(row), i1 = i0;
					while (i1 + 1 < 8 && (row >> (i1 + 1) & 1) != 0) {
						i1++;
					}
					row &= -1 << (i1 + 1);
					if (!any) {
						material = this.cells.material(nx, ny, nz);
						any = true;
					}
					this.corner(0, x, y, z, d, i0, j);
					this.corner(1, x, y, z, d, i1 + 1, j);
					this.corner(2, x, y, z, d, i1 + 1, j + 1);
					this.corner(3, x, y, z, d, i0, j + 1);
					this.out.quad(this.corners, d ^ 1, material, this.uv);
					this.quads++;
				}
			}
		}

		/**
		 * Whether the neighbour toward d is rock behind cell (i, j) of the face: its sub-voxel at the
		 * middle of the cell, moved half a sub-voxel into the neighbour (the slice against the face).
		 */
		private boolean occupied(long[] rock, int d, int i, int j) {
			frame(d, (i + 0.5) / 8, (j + 0.5) / 8, this.p);
			int vx = (int) Math.floor(8 * (this.p[0] - 15.0 / 16 * STEP[d][0]));
			int vy = (int) Math.floor(8 * (this.p[1] - 15.0 / 16 * STEP[d][1]));
			int vz = (int) Math.floor(8 * (this.p[2] - 15.0 / 16 * STEP[d][2]));
			return (rock[vy] >>> (vz * 8 + vx) & 1L) != 0;
		}

		/** Corner k of the quad: the face's point (a, b) / 8, section-relative, with its uv (u, 1 - v). */
		private void corner(int k, int x, int y, int z, int d, int a, int b) {
			double u = a / 8.0, v = b / 8.0;
			frame(d, u, v, this.p);
			this.corners[k * 3] = (float) (this.p[0] + x - 16 * this.sx);
			this.corners[k * 3 + 1] = (float) (this.p[1] + y - 16 * this.sy);
			this.corners[k * 3 + 2] = (float) (this.p[2] + z - 16 * this.sz);
			this.uv[k * 2] = (float) u;
			this.uv[k * 2 + 1] = (float) (1 - v);
		}
	}
}
