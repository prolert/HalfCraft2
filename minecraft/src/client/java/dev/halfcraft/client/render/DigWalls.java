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
 * sub-voxel past the line on that side isn't rock, nor dug) and nothing else of Half-Life's on that side of
 * it in the sub-voxel: no other face crossing it there, nor one along its side there that bounds something
 * inside it; along the whole of it, or at a brush's outer corner, where two such faces end at one point.
 * Elsewhere (an inner corner, a step finer than an eighth of a block, a face inside the rock, the void,
 * props) it's drawn whole, as before: cutting only ever takes away what's in the open air.
 */
final class DigWalls {
	// by Direction ordinal: down, up, north, south, west, east
	private static final int[][] STEP = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };
	/** Half-Life's faces are cut across a wall this far (blocks) into the neighbour: past the wall, where the pieces of a brush cut at it start. */
	private static final double INTO = 1.0e-4;
	/** Lengths below this (blocks) are none, and a line this close to a sub-voxel's corners doesn't cross it. */
	private static final double EPS = 1.0e-5;
	/**
	 * Lines and points closer than a face's tolerance (blocks) are one: 8 float ulps of the cell's coordinates (the
	 * triangles come as floats, a map's slot puts it up to tens of thousands of blocks along x), at least this and at
	 * most {@link #SAME_MAX}, well under the 1 unit (0.025 blocks) apart that two faces of Half-Life's grid are.
	 */
	private static final double SAME_MIN = 1.0e-3;
	private static final double SAME_MAX = 5.0e-3;
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

	/**
	 * What {@link #add} drew: its quads, how many of them are cut at Half-Life's surface, and what the cutting took
	 * (the triangles it looked at, and the time spent finding where to cut, in nanoseconds).
	 */
	record Count(int quads, int cut, int triangles, long cuttingNanos) {
	}

	private DigWalls() {
	}

	/** Adds the walls around the dug cells of section (sx, sy, sz) ({@code dug}: DugBits, left as they are). */
	static Count add(int sx, int sy, int sz, long[] dug, Cells cells, Out out) {
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
		return new Count(walls.quads, walls.cutQuads, walls.triangles, walls.cuttingNanos);
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

	/** The tolerance of the faces of cell (x, y, z): see {@link #SAME_MIN}. */
	private static double tolerance(int x, int y, int z) {
		float far = (float) (Math.max(Math.abs((double) x), Math.max(Math.abs((double) y), Math.abs((double) z))) + 2);
		return Math.clamp(8 * Math.ulp(far), SAME_MIN, SAME_MAX);
	}

	/**
	 * Where a Half-Life face crosses the face being walled: from (u0, v0) to (u1, v1), the side its face faces
	 * toward (au, av) (a unit vector across the line; for a diggable face, its brush's outside).
	 */
	private record Crossing(double u0, double v0, double u1, double v1, double au, double av, boolean diggable) {
		double length() {
			return Math.hypot(this.u1 - this.u0, this.v1 - this.v0);
		}
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
		/** In the sub-voxel looked at: the ends of the stretches inside it, u and v in turn (as many as there are). */
		double[] ends = new double[8];
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

		void end(double t) {
			if (this.endCount + 2 > this.ends.length) {
				this.ends = Arrays.copyOf(this.ends, this.ends.length * 2);
			}
			this.ends[this.endCount++] = this.u(t);
			this.ends[this.endCount++] = this.v(t);
		}

		/** Sorts the stretches and joins those that meet or come within {@code gap} of each other. */
		void join(double gap) {
			double[] s = this.spans;
			int n = this.spanCount / 2;
			for (int i = 1; i < n; i++) { // insertion sort by start: a face crosses in a few pieces
				double from = s[i * 2], to = s[i * 2 + 1];
				int j = i - 1;
				for (; j >= 0 && s[j * 2] > from; j--) {
					s[j * 2 + 2] = s[j * 2];
					s[j * 2 + 3] = s[j * 2 + 1];
				}
				s[j * 2 + 2] = from;
				s[j * 2 + 3] = to;
			}
			int count = 0;
			for (int i = 0; i < n; i++) {
				double from = s[i * 2], to = s[i * 2 + 1];
				if (count > 0 && from <= s[count - 1] + gap) {
					s[count - 1] = Math.max(s[count - 1], to);
				} else {
					s[count++] = from;
					s[count++] = to;
				}
			}
			this.spanCount = count;
		}
	}

	private static final class Walls {
		final int sx, sy, sz;
		final Cells cells;
		final Out out;
		int quads;
		int cutQuads; // of quads, those cut at Half-Life's surface
		int triangles; // looked at for the cutting
		long cuttingNanos;
		private final double[] p = new double[3];
		private final double[] f = new double[3];
		private final double[] lo = new double[3], hi = new double[3];
		private final double[] tu = new double[3], tv = new double[3], tw = new double[3]; // a triangle's corners on the face
		private final float[] corners = new float[12];
		private final float[] uv = new float[8];
		private final int[] mask = new int[8]; // the neighbour's rock against the face: bit i of row j
		private final long[] cutBy = new long[64]; // for each sub-voxel (i + 8 j): the rims cutting it, by bit
		private final double[] chord = new double[2];
		private final double[] part = new double[2]; // a crossing's part in a box, as fractions of it (see within)
		private final List<HostTri> tris = new ArrayList<>();
		private final List<Crossing> crossings = new ArrayList<>();
		private final List<Crossing> byLength = new ArrayList<>();
		private final List<Rim> rims = new ArrayList<>();
		private double[] polyU = new double[16], polyV = new double[16], nextU = new double[16], nextV = new double[16];
		// the face being walled: dug cell (x, y, z), toward d, its neighbour's rock and its tolerance
		private int x, y, z, d;
		private long[] rock;
		private double same;

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
			this.same = tolerance(x, y, z);
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
			long start = System.nanoTime();
			this.gatherCrossings();
			for (int j = 0; j < 8; j++) {
				for (int i = 0; i < 8; i++) {
					this.cutBy[i + 8 * j] = this.rims.isEmpty() || (this.mask[j] >> i & 1) == 0 ? 0L : this.clips(i, j);
				}
			}
			this.cuttingNanos += System.nanoTime() - start;
			for (int j = 0; j < 8; j++) {
				int row = this.mask[j];
				while (row != 0) {
					int i0 = Integer.numberOfTrailingZeros(row), i1 = i0;
					long clips = this.cutBy[i0 + 8 * j];
					while (i1 + 1 < 8 && (row >> (i1 + 1) & 1) != 0 && this.cutBy[i1 + 1 + 8 * j] == clips) {
						i1++;
					}
					row &= -1 << (i1 + 1);
					this.emit(i0 / 8.0, j / 8.0, (i1 + 1) / 8.0, (j + 1) / 8.0, clips, material);
				}
			}
		}

		/**
		 * Whether the neighbour is rock behind the face's point (u, v): its sub-voxel there, half a sub-voxel into
		 * it (the slice against the face). Past the face's edge, the neighbour's own neighbour's slice. One that
		 * isn't known yet is all rock; so is a dug one, as nothing tells what was there (one Minecraft revealed was
		 * rock throughout, and Half-Life draws nothing where its geometry was cut away).
		 */
		private boolean rockAt(double u, double v) {
			frame(this.d, u, v, this.p);
			double rx = this.p[0] - 15.0 / 16 * STEP[this.d][0], ry = this.p[1] - 15.0 / 16 * STEP[this.d][1], rz = this.p[2] - 15.0 / 16 * STEP[this.d][2];
			int ox = (int) Math.floor(rx), oy = (int) Math.floor(ry), oz = (int) Math.floor(rz);
			long[] rock = this.rock;
			if (ox != 0 || oy != 0 || oz != 0) {
				int cx = this.x + STEP[this.d][0] + ox, cy = this.y + STEP[this.d][1] + oy, cz = this.z + STEP[this.d][2] + oz;
				if (this.cells.dug(cx, cy, cz)) {
					return true;
				}
				rock = this.cells.rock(cx, cy, cz);
				if (rock == null) {
					return true;
				}
			}
			int vx = Math.min(7, (int) Math.floor(8 * (rx - ox))), vy = Math.min(7, (int) Math.floor(8 * (ry - oy))), vz = Math.min(7, (int) Math.floor(8 * (rz - oz)));
			return (rock[vy] >>> (vz * 8 + vx) & 1L) != 0;
		}

		/**
		 * Where Half-Life's faces cross the face just inside the neighbour (crossings), and the lines its diggable
		 * ones make (rims), each along its longest piece.
		 */
		private void gatherCrossings() {
			this.tris.clear();
			this.crossings.clear();
			this.byLength.clear();
			this.rims.clear();
			double[] lo = this.lo, hi = this.hi;
			frame(this.d, 0, 0, lo);
			frame(this.d, 1, 1, hi);
			for (int k = 0; k < 3; k++) {
				double a = Math.min(lo[k], hi[k]), b = Math.max(lo[k], hi[k]);
				if (STEP[this.d][k] != 0) { // across the wall: around where the faces are cut, no further
					double w0 = a + STEP[this.d][k] * INTO / 2, w1 = a + STEP[this.d][k] * 2 * INTO;
					a = Math.min(w0, w1);
					b = Math.max(w0, w1);
				}
				lo[k] = a;
				hi[k] = b;
			}
			this.cells.triangles(this.x + lo[0], this.y + lo[1], this.z + lo[2], this.x + hi[0], this.y + hi[1], this.z + hi[2], this.tris);
			this.triangles += this.tris.size();
			double[] u = this.tu, v = this.tv, w = this.tw;
			for (HostTri t : this.tris) {
				this.toFace(t.ax, t.ay, t.az, 0);
				this.toFace(t.bx, t.by, t.bz, 1);
				this.toFace(t.cx, t.cy, t.cz, 2);
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
				double length = Math.hypot(u1 - u0, v1 - v0);
				if (n != 2 || length < EPS) {
					continue;
				}
				alongFace(this.d, t.nx, t.ny, t.nz, this.f);
				if (Math.hypot(this.f[0], this.f[1]) < ACROSS) {
					continue;
				}
				// across the piece itself (a small triangle's normal is the less sure), the way its face faces
				double au = -(v1 - v0) / length, av = (u1 - u0) / length;
				if (au * this.f[0] + av * this.f[1] < 0) {
					au = -au;
					av = -av;
				}
				Crossing crossing = new Crossing(u0, v0, u1, v1, au, av, t.diggable());
				this.crossings.add(crossing);
				if (crossing.diggable()) {
					this.byLength.add(crossing);
				}
			}
			this.byLength.sort((a, b) -> Double.compare(b.length(), a.length()));
			for (Crossing crossing : this.byLength) {
				this.rim(crossing);
			}
			for (Rim rim : this.rims) {
				rim.join(this.same);
			}
		}

		/** Corner k of a triangle, as (u, v, w) of the face. */
		private void toFace(double cx, double cy, double cz, int k) {
			onFace(this.d, cx - this.x, cy - this.y, cz - this.z, this.f);
			this.tu[k] = this.f[0];
			this.tv[k] = this.f[1];
			this.tw[k] = this.f[2];
		}

		/** A diggable face's crossing onto the rim of its line (another piece of the same face made it already, mostly). */
		private void rim(Crossing crossing) {
			for (Rim rim : this.rims) {
				if (rim.au * crossing.au + rim.av * crossing.av > 0.999 && Math.abs(rim.past(crossing.u0, crossing.v0)) <= this.same
					&& Math.abs(rim.past(crossing.u1, crossing.v1)) <= this.same) {
					rim.span(rim.t(crossing.u0, crossing.v0), rim.t(crossing.u1, crossing.v1));
					return;
				}
			}
			Rim rim = new Rim(crossing.au, crossing.av, crossing.au * crossing.u0 + crossing.av * crossing.v0);
			rim.span(rim.t(crossing.u0, crossing.v0), rim.t(crossing.u1, crossing.v1));
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
					covered |= from <= tA + this.same && to >= tB - this.same;
					if (to - from > longest) {
						longest = to - from;
						middle = (from + to) * 0.5;
					}
					if (from > tA + this.same) {
						rim.end(from);
					}
					if (to < tB - this.same) {
						rim.end(to);
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
					if (this.meet(this.rims.get(r), this.rims.get(s))) {
						corners |= 1L << r | 1L << s;
					}
				}
			}
			return whole | corners;
		}

		/** Two rims at an angle, a stretch of each ending at one point in the sub-voxel: a brush's outer corner. */
		private boolean meet(Rim a, Rim b) {
			if (Math.abs(a.au * b.av - a.av * b.au) < 0.1) {
				return false; // parallel: two brushes' faces, not a corner of one
			}
			for (int k = 0; k < a.endCount; k += 2) {
				for (int m = 0; m < b.endCount; m += 2) {
					if (Math.hypot(a.ends[k] - b.ends[m], a.ends[k + 1] - b.ends[m + 1]) <= this.same) {
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
		 * Whether something of Half-Life's other than air lies past the rim in the sub-voxel [u0, u1] x [v0, v1], so the
		 * rim doesn't cut it: another face crossing the wall there (any, props too: a brush under a ceiling's face, the
		 * floor at a wall's foot), or one along a side of the sub-voxel that bounds what's inside it (its brush inside,
		 * as its winding faces out of the sub-voxel; a prop or a displacement whatever its winding), such as a brush
		 * thinner than a sub-voxel on the rim's face, whose sub-voxel the voxels leave out.
		 */
		private boolean crowded(Rim rim, double u0, double v0, double u1, double v1) {
			double same = this.same;
			for (Crossing g : this.crossings) {
				if (rim.past(g.u0(), g.v0()) <= same && rim.past(g.u1(), g.v1()) <= same) {
					continue; // on the rock side, or along the rim itself
				}
				if ((this.within(g, u0 + same, v0 + same, u1 - same, v1 - same) && this.past(rim, g)) || this.alongSide(rim, g, u0, v0, u1, v1)) {
					return true;
				}
			}
			return false;
		}

		/**
		 * Whether the crossing lies along a side of the sub-voxel [u0, u1] x [v0, v1], bounding what's inside it (its
		 * winding faces out of the sub-voxel; any winding for a face that isn't diggable), and past the rim there.
		 */
		private boolean alongSide(Rim rim, Crossing g, double u0, double v0, double u1, double v1) {
			double same = this.same;
			boolean anyWinding = !g.diggable();
			if (Math.abs(g.u0() - u0) <= same && Math.abs(g.u1() - u0) <= same && (anyWinding || g.au() < -0.5)) {
				return this.within(g, u0 - same, v0, u0 + same, v1) && this.past(rim, g);
			}
			if (Math.abs(g.u0() - u1) <= same && Math.abs(g.u1() - u1) <= same && (anyWinding || g.au() > 0.5)) {
				return this.within(g, u1 - same, v0, u1 + same, v1) && this.past(rim, g);
			}
			if (Math.abs(g.v0() - v0) <= same && Math.abs(g.v1() - v0) <= same && (anyWinding || g.av() < -0.5)) {
				return this.within(g, u0, v0 - same, u1, v0 + same) && this.past(rim, g);
			}
			if (Math.abs(g.v0() - v1) <= same && Math.abs(g.v1() - v1) <= same && (anyWinding || g.av() > 0.5)) {
				return this.within(g, u0, v1 - same, u1, v1 + same) && this.past(rim, g);
			}
			return false;
		}

		/** Whether the crossing has a part of some length in the box [u0, u1] x [v0, v1]: from part[0] to part[1] of it. */
		private boolean within(Crossing g, double u0, double v0, double u1, double v1) {
			double du = g.u1() - g.u0(), dv = g.v1() - g.v0();
			this.part[0] = 0;
			this.part[1] = 1;
			// u0 <= g.u0 + s du <= u1 and v0 <= g.v0 + s dv <= v1
			return this.narrow(-du, g.u0() - u0) && this.narrow(du, u1 - g.u0()) && this.narrow(-dv, g.v0() - v0) && this.narrow(dv, v1 - g.v0())
				&& (this.part[1] - this.part[0]) * g.length() > EPS;
		}

		/** Narrows part to where p s <= q; false when none of it is left. */
		private boolean narrow(double p, double q) {
			if (Math.abs(p) < 1.0e-15) {
				return q >= 0;
			}
			if (p < 0) {
				this.part[0] = Math.max(this.part[0], q / p);
			} else {
				this.part[1] = Math.min(this.part[1], q / p);
			}
			return this.part[0] <= this.part[1];
		}

		/** Whether the crossing's part (see within) lies past the rim somewhere. */
		private boolean past(Rim rim, Crossing g) {
			double du = g.u1() - g.u0(), dv = g.v1() - g.v0();
			return rim.past(g.u0() + du * this.part[0], g.v0() + dv * this.part[0]) > this.same || rim.past(g.u0() + du * this.part[1], g.v0() + dv * this.part[1]) > this.same;
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
				if (clips != 0) {
					this.cutQuads++;
				}
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
