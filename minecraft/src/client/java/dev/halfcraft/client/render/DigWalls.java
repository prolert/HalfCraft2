package dev.halfcraft.client.render;

import dev.halfcraft.world.HostTri;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * The walls of holes dug into Half-Life's walls (from SkyCraft's DigWalls, by rock alone). Where a dug
 * cell meets a neighbour that isn't dug, isn't filled by a block that hides what's behind it and is
 * partly rock (Half-Life's geometry or its void beyond the map), the neighbour's slice of sub-voxels
 * against the face is drawn on it, an eighth of a block at a time, in the texture of the neighbour's
 * material: the hole is closed all round and nothing shows through Half-Life's cut walls.
 * <p>
 * The sub-voxels round a brush out to the next eighth of a block, so where one of its faces meets the
 * map's open air across the wall (a ceiling's underside round a hole in it, a wall's face beside a
 * doorway dug through it) the wall stood out past the surface Half-Life draws. There it's cut along the
 * line where that face crosses the wall, just inside the neighbour, as SkyCraft cuts its walls along the
 * land's triangles: it ends at Half-Life's surface. A sub-voxel is cut by a diggable face (a world
 * brush's, whose winding faces out) that crosses it, with the open air of the map past it (the slice's
 * sub-voxel past the line on that side isn't rock) and no other face of Half-Life's on that side of it in
 * the sub-voxel; along the whole of it, or at a brush's outer corner, where two such faces end at one
 * point. Elsewhere (an inner corner, a step finer than an eighth of a block, a face inside the rock, the
 * void, props) it's drawn whole, as before: cutting only ever takes away what's in the open air.
 */
final class DigWalls {
	// by Direction ordinal: down, up, north, south, west, east
	private static final int[][] STEP = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };
	/** Half-Life's faces are cut across a wall this far (blocks) into the neighbour: past the wall, where the pieces of a brush cut at it start. */
	private static final double INTO = 1.0e-4;
	/** Lengths below this (blocks) are none. */
	private static final double EPS = 1.0e-5;
	/** Lines and points this close (blocks) are one. */
	private static final double SAME = 1.0e-4;
	/** A face whose normal lies along the wall's less than this doesn't cut it (it runs along the wall). */
	private static final double ACROSS = 0.05;
	/** How far (blocks) past a sub-voxel a line's open air is looked for. */
	private static final double PAST = 1.0 / 16;

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

		/** Adds every Half-Life triangle whose bounds overlap the box (HostCollision.trianglesNear). */
		void triangles(double minX, double minY, double minZ, double maxX, double maxY, double maxZ, List<HostTri> out);
	}

	interface Out {
		/**
		 * One quad of a wall: four corners (x, y, z each, section-relative) facing {@code face} (a Direction
		 * ordinal), and their uvs (u, v each, 0-1 across the face of a block of the Proto.DIG_* {@code material},
		 * v down). The arrays are used again for the next quad. A cut sub-voxel's triangle repeats its last corner.
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

	/** {@link #frame} the other way: a point given from the dug cell's min corner as (u, v) on the face toward d and w past it, into the neighbour. */
	private static void onFace(int d, double x, double y, double z, double[] f) {
		switch (d) {
			case 0 -> set(f, x, 1 - z, -y);
			case 1 -> set(f, x, z, y - 1);
			case 2 -> set(f, x, y, -z);
			case 3 -> set(f, 1 - x, y, z - 1);
			case 4 -> set(f, 1 - z, y, -x);
			default -> set(f, z, y, x - 1);
		}
	}

	/** A direction as (u, v, w) of the face toward d (see {@link #onFace}). */
	private static void alongFace(int d, double x, double y, double z, double[] f) {
		switch (d) {
			case 0 -> set(f, x, -z, -y);
			case 1 -> set(f, x, z, y);
			case 2 -> set(f, x, y, -z);
			case 3 -> set(f, -x, y, z);
			case 4 -> set(f, -z, y, -x);
			default -> set(f, z, y, x);
		}
	}

	private static void set(double[] p, double x, double y, double z) {
		p[0] = x;
		p[1] = y;
		p[2] = z;
	}

	/** Where a Half-Life face crosses the face being walled: from (u0, v0) to (u1, v1), the open air toward (au, av) (a unit vector) if it's a brush's. */
	private record Cut(double u0, double v0, double u1, double v1, double au, double av) {
	}

	/**
	 * A line diggable faces of Half-Life cross the face along: rock where au u + av v <= c, past it (toward
	 * (au, av)) what's outside their brush. Along it, t: u = c au - t av, v = c av + t au.
	 */
	private static final class Rim {
		final double au, av, c;
		/** The stretches of it the faces cover, from and to in turn, sorted and joined once all are in. */
		double[] spans = new double[8];
		int spanCount; // doubles in spans
		/** In the sub-voxel looked at: the ends of the stretches inside it, u and v in turn. */
		final double[] ends = new double[8];
		int endCount; // doubles in ends

		Rim(double au, double av, double c) {
			this.au = au;
			this.av = av;
			this.c = c;
		}

		/** How far (u, v) lies past the line, into the open air (negative: in the rock). */
		double past(double u, double v) {
			return this.au * u + this.av * v - this.c;
		}

		double t(double u, double v) {
			return -this.av * u + this.au * v;
		}

		double u(double t) {
			return this.c * this.au - t * this.av;
		}

		double v(double t) {
			return this.c * this.av + t * this.au;
		}

		void span(double t0, double t1) {
			if (this.spanCount + 2 > this.spans.length) {
				this.spans = Arrays.copyOf(this.spans, this.spans.length * 2);
			}
			this.spans[this.spanCount++] = Math.min(t0, t1);
			this.spans[this.spanCount++] = Math.max(t0, t1);
		}

		/** Sorts the stretches and joins those that touch. */
		void join() {
			int n = this.spanCount / 2;
			Integer[] order = new Integer[n];
			for (int k = 0; k < n; k++) {
				order[k] = k;
			}
			double[] spans = this.spans;
			Arrays.sort(order, (a, b) -> Double.compare(spans[a * 2], spans[b * 2]));
			double[] joined = new double[Math.max(2, n * 2)];
			int count = 0;
			for (int k : order) {
				double t0 = spans[k * 2], t1 = spans[k * 2 + 1];
				if (count > 0 && t0 <= joined[count - 1] + EPS) {
					joined[count - 1] = Math.max(joined[count - 1], t1);
				} else {
					joined[count++] = t0;
					joined[count++] = t1;
				}
			}
			this.spans = joined;
			this.spanCount = count;
		}
	}

	private static final class Walls {
		final int sx, sy, sz;
		final Cells cells;
		final Out out;
		int quads;
		private final double[] p = new double[3];
		private final double[] f = new double[3];
		private final float[] corners = new float[12];
		private final float[] uv = new float[8];
		private final int[] mask = new int[8]; // the neighbour's rock against the face: bit i of row j
		private final long[] cutBy = new long[8]; // for each sub-voxel of a row: the rims cutting it, by bit
		private final double[] chord = new double[2];
		private final List<HostTri> tris = new ArrayList<>();
		private final List<Cut> cuts = new ArrayList<>();
		private final List<Rim> rims = new ArrayList<>();
		private double[] polyU = new double[16], polyV = new double[16], nextU = new double[16], nextV = new double[16];
		// the face being walled: dug cell (x, y, z), toward d, and its neighbour's rock
		private int x, y, z, d;
		private long[] rock = new long[8];

		Walls(int sx, int sy, int sz, Cells cells, Out out) {
			this.sx = sx;
			this.sy = sy;
			this.sz = sz;
			this.cells = cells;
			this.out = out;
		}

		/**
		 * The wall between the dug cell (x, y, z) and its neighbour toward d: a quad for each run of rock along a
		 * row cut by the same lines (none, mostly), cut where Half-Life's surface crosses it.
		 */
		void face(int x, int y, int z, int d) {
			int nx = x + STEP[d][0], ny = y + STEP[d][1], nz = z + STEP[d][2];
			if (this.cells.dug(nx, ny, nz) || this.cells.occludes(nx, ny, nz)) {
				return;
			}
			long[] rock = this.cells.rock(nx, ny, nz);
			if (rock == null) {
				return;
			}
			this.x = x;
			this.y = y;
			this.z = z;
			this.d = d;
			this.rock = rock;
			boolean any = false;
			for (int j = 0; j < 8; j++) {
				int row = 0;
				for (int i = 0; i < 8; i++) {
					if (this.rockAt((i + 0.5) / 8, (j + 0.5) / 8)) {
						row |= 1 << i;
					}
				}
				this.mask[j] = row;
				any |= row != 0;
			}
			if (!any) {
				return;
			}
			int material = this.cells.material(nx, ny, nz);
			this.cut();
			for (int j = 0; j < 8; j++) {
				int row = this.mask[j];
				for (int i = 0; i < 8; i++) {
					this.cutBy[i] = this.rims.isEmpty() || (row >> i & 1) == 0 ? 0L : this.clips(i, j);
				}
				while (row != 0) {
					int i0 = Integer.numberOfTrailingZeros(row), i1 = i0;
					while (i1 + 1 < 8 && (row >> (i1 + 1) & 1) != 0 && this.cutBy[i1 + 1] == this.cutBy[i0]) {
						i1++;
					}
					row &= -1 << (i1 + 1);
					this.emit(i0 / 8.0, j / 8.0, (i1 + 1) / 8.0, (j + 1) / 8.0, this.cutBy[i0], material);
				}
			}
		}

		/**
		 * Whether the neighbour is rock behind the face's point (u, v): its sub-voxel there, half a sub-voxel into
		 * it (the slice against the face). Past the face's edge, the neighbour's own neighbour's slice: a dug one's
		 * no rock, one that isn't known yet all rock.
		 */
		private boolean rockAt(double u, double v) {
			frame(this.d, u, v, this.p);
			double rx = this.p[0] - 15.0 / 16 * STEP[this.d][0], ry = this.p[1] - 15.0 / 16 * STEP[this.d][1], rz = this.p[2] - 15.0 / 16 * STEP[this.d][2];
			int ox = (int) Math.floor(rx), oy = (int) Math.floor(ry), oz = (int) Math.floor(rz);
			long[] rock = this.rock;
			if (ox != 0 || oy != 0 || oz != 0) {
				int cx = this.x + STEP[this.d][0] + ox, cy = this.y + STEP[this.d][1] + oy, cz = this.z + STEP[this.d][2] + oz;
				if (this.cells.dug(cx, cy, cz)) {
					return false;
				}
				rock = this.cells.rock(cx, cy, cz);
				if (rock == null) {
					return true;
				}
			}
			int vx = Math.min(7, (int) Math.floor(8 * (rx - ox))), vy = Math.min(7, (int) Math.floor(8 * (ry - oy))), vz = Math.min(7, (int) Math.floor(8 * (rz - oz)));
			return (rock[vy] >>> (vz * 8 + vx) & 1L) != 0;
		}

		/** Where Half-Life's faces cross the face just inside the neighbour (cuts), and the lines its diggable ones make (rims). */
		private void cut() {
			this.tris.clear();
			this.cuts.clear();
			this.rims.clear();
			double[] lo = new double[3], hi = new double[3];
			frame(this.d, 0, 0, lo);
			frame(this.d, 1, 1, hi);
			for (int k = 0; k < 3; k++) {
				double a = Math.min(lo[k], hi[k]), b = Math.max(lo[k], hi[k]);
				lo[k] = a - SAME + Math.min(0, STEP[this.d][k]) * 2 * INTO;
				hi[k] = b + SAME + Math.max(0, STEP[this.d][k]) * 2 * INTO;
			}
			this.cells.triangles(this.x + lo[0], this.y + lo[1], this.z + lo[2], this.x + hi[0], this.y + hi[1], this.z + hi[2], this.tris);
			double[] u = new double[3], v = new double[3], w = new double[3];
			for (HostTri t : this.tris) {
				this.toFace(t.ax, t.ay, t.az, 0, u, v, w);
				this.toFace(t.bx, t.by, t.bz, 1, u, v, w);
				this.toFace(t.cx, t.cy, t.cz, 2, u, v, w);
				int n = 0;
				double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
				for (int k = 0; k < 3; k++) {
					int m = (k + 1) % 3;
					double dk = w[k] - INTO, dm = w[m] - INTO;
					if ((dk < 0) != (dm < 0)) {
						double s = dk / (dk - dm);
						if (n++ == 0) {
							u0 = u[k] + (u[m] - u[k]) * s;
							v0 = v[k] + (v[m] - v[k]) * s;
						} else {
							u1 = u[k] + (u[m] - u[k]) * s;
							v1 = v[k] + (v[m] - v[k]) * s;
						}
					}
				}
				if (n != 2 || Math.hypot(u1 - u0, v1 - v0) < EPS) {
					continue;
				}
				alongFace(this.d, t.nx, t.ny, t.nz, this.f);
				double across = Math.hypot(this.f[0], this.f[1]);
				if (across < ACROSS) {
					continue;
				}
				Cut cut = new Cut(u0, v0, u1, v1, this.f[0] / across, this.f[1] / across);
				this.cuts.add(cut);
				if (t.diggable()) {
					this.rim(cut);
				}
			}
			for (Rim rim : this.rims) {
				rim.join();
			}
		}

		/** Corner k of a triangle, as (u, v, w) of the face. */
		private void toFace(double cx, double cy, double cz, int k, double[] u, double[] v, double[] w) {
			onFace(this.d, cx - this.x, cy - this.y, cz - this.z, this.f);
			u[k] = this.f[0];
			v[k] = this.f[1];
			w[k] = this.f[2];
		}

		/** A diggable face's cut onto the rim of its line (another face of the same plane, or a piece of it, made it already, mostly). */
		private void rim(Cut cut) {
			for (Rim rim : this.rims) {
				if (rim.au * cut.au + rim.av * cut.av > 0.999 && Math.abs(rim.past(cut.u0, cut.v0)) <= SAME && Math.abs(rim.past(cut.u1, cut.v1)) <= SAME) {
					rim.span(rim.t(cut.u0, cut.v0), rim.t(cut.u1, cut.v1));
					return;
				}
			}
			Rim rim = new Rim(cut.au, cut.av, cut.au * cut.u0 + cut.av * cut.v0);
			rim.span(rim.t(cut.u0, cut.v0), rim.t(cut.u1, cut.v1));
			this.rims.add(rim);
		}

		/**
		 * The rims that cut the rock sub-voxel (i, j) (by bit, the first 64): those crossing it with the open air past
		 * them, along the whole of it, or two that end at one point in it (a brush's outer corner).
		 */
		private long clips(int i, int j) {
			double u0 = i / 8.0, v0 = j / 8.0, u1 = (i + 1) / 8.0, v1 = (j + 1) / 8.0;
			long whole = 0L, ending = 0L;
			int count = Math.min(this.rims.size(), 64);
			for (int r = 0; r < count; r++) {
				Rim rim = this.rims.get(r);
				rim.endCount = 0;
				double a = rim.past(u0, v0), b = rim.past(u1, v0), c = rim.past(u1, v1), e = rim.past(u0, v1);
				if (Math.max(Math.max(a, b), Math.max(c, e)) <= EPS || Math.min(Math.min(a, b), Math.min(c, e)) >= -EPS) {
					continue; // it doesn't cross the sub-voxel
				}
				if (!this.chord(rim, u0, v0, u1, v1)) {
					continue;
				}
				double tA = this.chord[0], tB = this.chord[1];
				boolean covered = false; // a stretch covers the whole chord
				double longest = 0, middle = 0;
				for (int k = 0; k < rim.spanCount; k += 2) {
					double from = Math.max(rim.spans[k], tA), to = Math.min(rim.spans[k + 1], tB);
					if (to - from <= EPS) {
						continue;
					}
					covered |= from <= tA + EPS && to >= tB - EPS;
					if (to - from > longest) {
						longest = to - from;
						middle = (from + to) * 0.5;
					}
					if (from > tA + EPS) {
						this.end(rim, from);
					}
					if (to < tB - EPS) {
						this.end(rim, to);
					}
				}
				if (longest <= EPS || !this.openPast(rim, rim.u(middle), rim.v(middle), u0, v0, u1, v1) || this.crowded(rim, u0, v0, u1, v1)) {
					continue;
				}
				if (covered) {
					whole |= 1L << r;
				} else {
					ending |= 1L << r;
				}
			}
			long corners = 0L;
			for (long p = ending; p != 0; p &= p - 1) {
				int r = Long.numberOfTrailingZeros(p);
				for (long q = p & p - 1; q != 0; q &= q - 1) {
					int s = Long.numberOfTrailingZeros(q);
					if (meet(this.rims.get(r), this.rims.get(s))) {
						corners |= 1L << r | 1L << s;
					}
				}
			}
			return whole | corners;
		}

		/** Notes an end of a stretch of the rim inside the sub-voxel. */
		private void end(Rim rim, double t) {
			if (rim.endCount + 2 <= rim.ends.length) {
				rim.ends[rim.endCount++] = rim.u(t);
				rim.ends[rim.endCount++] = rim.v(t);
			}
		}

		/** Two rims at an angle, a stretch of each ending at one point in the sub-voxel: a brush's outer corner. */
		private static boolean meet(Rim a, Rim b) {
			if (Math.abs(a.au * b.av - a.av * b.au) < 0.1) {
				return false; // parallel: two brushes' faces, not a corner of one
			}
			for (int k = 0; k < a.endCount; k += 2) {
				for (int m = 0; m < b.endCount; m += 2) {
					if (Math.hypot(a.ends[k] - b.ends[m], a.ends[k + 1] - b.ends[m + 1]) <= SAME) {
						return true;
					}
				}
			}
			return false;
		}

		/** The rim's line in the square [u0, u1] x [v0, v1], as t from chord[0] to chord[1]; false when it isn't in it. */
		private boolean chord(Rim rim, double u0, double v0, double u1, double v1) {
			double lo = Double.NEGATIVE_INFINITY, hi = Double.POSITIVE_INFINITY;
			if (Math.abs(rim.av) > 1.0e-12) { // u = c au - t av
				double a = (rim.c * rim.au - u0) / rim.av, b = (rim.c * rim.au - u1) / rim.av;
				lo = Math.max(lo, Math.min(a, b));
				hi = Math.min(hi, Math.max(a, b));
			}
			if (Math.abs(rim.au) > 1.0e-12) { // v = c av + t au
				double a = (v0 - rim.c * rim.av) / rim.au, b = (v1 - rim.c * rim.av) / rim.au;
				lo = Math.max(lo, Math.min(a, b));
				hi = Math.min(hi, Math.max(a, b));
			}
			this.chord[0] = lo;
			this.chord[1] = hi;
			return hi - lo > EPS;
		}

		/**
		 * Whether the map's open air lies past the rim at (u, v) in the sub-voxel [u0, u1] x [v0, v1]: the slice
		 * isn't rock just out of the sub-voxel that way. Rock there makes it a face inside the rock (of a brush
		 * against another, or against the void), which cuts nothing.
		 */
		private boolean openPast(Rim rim, double u, double v, double u0, double v0, double u1, double v1) {
			double out = Double.POSITIVE_INFINITY;
			if (rim.au > 1.0e-9) {
				out = Math.min(out, (u1 - u) / rim.au);
			} else if (rim.au < -1.0e-9) {
				out = Math.min(out, (u0 - u) / rim.au);
			}
			if (rim.av > 1.0e-9) {
				out = Math.min(out, (v1 - v) / rim.av);
			} else if (rim.av < -1.0e-9) {
				out = Math.min(out, (v0 - v) / rim.av);
			}
			double k = Math.max(out, 0) + PAST;
			return !this.rockAt(u + rim.au * k, v + rim.av * k);
		}

		/**
		 * Whether another face of Half-Life's (any, not only a diggable one) lies past the rim in the sub-voxel
		 * [u0, u1] x [v0, v1]: something other than air is on that side (a brush under a ceiling's face there,
		 * the floor at a wall's foot), so the rim doesn't cut it.
		 */
		private boolean crowded(Rim rim, double u0, double v0, double u1, double v1) {
			for (Cut g : this.cuts) {
				double a = rim.past(g.u0, g.v0), b = rim.past(g.u1, g.v1);
				if (a <= SAME && b <= SAME) {
					continue; // on the rock side, or along the rim itself
				}
				// the part of it inside the sub-voxel (a face along the sub-voxel's side isn't in it)
				double s0 = 0, s1 = 1;
				double du = g.u1 - g.u0, dv = g.v1 - g.v0;
				double[][] sides = { { -du, g.u0 - (u0 + EPS) }, { du, (u1 - EPS) - g.u0 }, { -dv, g.v0 - (v0 + EPS) }, { dv, (v1 - EPS) - g.v0 } };
				boolean inside = true;
				for (double[] side : sides) {
					double pk = side[0], qk = side[1];
					if (Math.abs(pk) < 1.0e-15) {
						inside &= qk >= 0;
					} else if (pk < 0) {
						s0 = Math.max(s0, qk / pk);
					} else {
						s1 = Math.min(s1, qk / pk);
					}
				}
				if (!inside || s1 - s0 <= 0) {
					continue;
				}
				if (rim.past(g.u0 + du * s0, g.v0 + dv * s0) > SAME || rim.past(g.u0 + du * s1, g.v0 + dv * s1) > SAME) {
					return true;
				}
			}
			return false;
		}

		/** The rectangle [u0, u1] x [v0, v1] of the face, cut back to the rock side of the rims in {@code clips}, as quads. */
		private void emit(double u0, double v0, double u1, double v1, long clips, int material) {
			double[] pu = this.polyU, pv = this.polyV;
			pu[0] = u0;
			pv[0] = v0;
			pu[1] = u1;
			pv[1] = v0;
			pu[2] = u1;
			pv[2] = v1;
			pu[3] = u0;
			pv[3] = v1;
			int n = 4;
			for (long bits = clips; bits != 0 && n >= 3; bits &= bits - 1) {
				n = this.clip(this.rims.get(Long.numberOfTrailingZeros(bits)), n);
				pu = this.polyU;
				pv = this.polyV;
			}
			if (n < 3) {
				return;
			}
			double area = 0;
			for (int k = 0; k < n; k++) {
				int m = (k + 1) % n;
				area += pu[k] * pv[m] - pu[m] * pv[k];
			}
			if (area < 1.0e-9) {
				return;
			}
			// a fan of quads, the last one a triangle (its last corner twice) for an odd number of corners
			for (int k = 1; k + 1 < n; k += 2) {
				int last = Math.min(k + 2, n - 1);
				this.corner(0, pu[0], pv[0]);
				this.corner(1, pu[k], pv[k]);
				this.corner(2, pu[k + 1], pv[k + 1]);
				this.corner(3, pu[last], pv[last]);
				this.out.quad(this.corners, this.d ^ 1, material, this.uv);
				this.quads++;
			}
		}

		/** Cuts the polygon (polyU, polyV; n corners, counter-clockwise) back to the rock side of the rim; returns its corners. */
		private int clip(Rim rim, int n) {
			if (this.nextU.length < n * 2 + 2) {
				this.nextU = Arrays.copyOf(this.nextU, n * 2 + 2);
				this.nextV = Arrays.copyOf(this.nextV, n * 2 + 2);
			}
			double[] pu = this.polyU, pv = this.polyV, qu = this.nextU, qv = this.nextV;
			int m = 0;
			for (int k = 0; k < n; k++) {
				int l = (k + 1) % n;
				double a = rim.past(pu[k], pv[k]), b = rim.past(pu[l], pv[l]);
				if (a <= 0) {
					qu[m] = pu[k];
					qv[m++] = pv[k];
				}
				if ((a < 0 && b > 0) || (a > 0 && b < 0)) {
					double s = a / (a - b);
					qu[m] = pu[k] + (pu[l] - pu[k]) * s;
					qv[m++] = pv[k] + (pv[l] - pv[k]) * s;
				}
			}
			this.polyU = qu;
			this.polyV = qv;
			this.nextU = pu;
			this.nextV = pv;
			return m;
		}

		/** Corner k of the quad: the face's point (u, v), section-relative, with its uv (u, 1 - v). */
		private void corner(int k, double u, double v) {
			frame(this.d, u, v, this.p);
			this.corners[k * 3] = (float) (this.p[0] + this.x - 16 * this.sx);
			this.corners[k * 3 + 1] = (float) (this.p[1] + this.y - 16 * this.sy);
			this.corners[k * 3 + 2] = (float) (this.p[2] + this.z - 16 * this.sz);
			this.uv[k * 2] = (float) u;
			this.uv[k * 2 + 1] = (float) (1 - v);
		}
	}
}
