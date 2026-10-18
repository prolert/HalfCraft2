package dev.halfcraft.world;

/**
 * A chunk section's dug cells as 64 longs: bit {@code x + 16z + 256y} of the cell (each 0-15 within the
 * section) is bit {@code index & 63} of long {@code index >> 6}.
 */
public final class DugBits {
	/** How many longs a section's bits take. */
	public static final int LONGS = 64;
	private static final int BYTES = LONGS * Long.BYTES;

	private DugBits() {
	}

	/** The bit of the cell (x, y, z), in whatever section it lies. */
	public static int index(int x, int y, int z) {
		return (x & 15) + 16 * (z & 15) + 256 * (y & 15);
	}

	public static boolean get(long[] bits, int index) {
		return (bits[index >> 6] >>> (index & 63) & 1L) != 0;
	}

	public static void set(long[] bits, int index) {
		bits[index >> 6] |= 1L << (index & 63);
	}

	/** How many cells are dug. */
	public static int count(long[] bits) {
		int count = 0;
		for (long word : bits) {
			count += Long.bitCount(word);
		}
		return count;
	}

	/** The bits as 512 bytes, as Half-Life reads them: bit i is bit {@code i & 7} of byte {@code i >> 3}. */
	public static byte[] toBytes(long[] bits) {
		byte[] bytes = new byte[BYTES];
		for (int i = 0; i < BYTES; i++) {
			bytes[i] = (byte) (bits[i >> 3] >>> ((i & 7) * 8));
		}
		return bytes;
	}
}
