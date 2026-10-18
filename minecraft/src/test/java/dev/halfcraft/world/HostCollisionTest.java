package dev.halfcraft.world;

import static dev.halfcraft.link.Proto.COL_REGION_COUNT;
import static dev.halfcraft.link.Proto.COL_REGION_EPOCH;
import static dev.halfcraft.link.Proto.COL_REGION_HEADER_BYTES;
import static dev.halfcraft.link.Proto.COL_REGION_MAX_X;
import static dev.halfcraft.link.Proto.COL_REGION_MAX_Y;
import static dev.halfcraft.link.Proto.COL_REGION_MAX_Z;
import static dev.halfcraft.link.Proto.COL_REGION_MIN_X;
import static dev.halfcraft.link.Proto.COL_REGION_MIN_Y;
import static dev.halfcraft.link.Proto.COL_REGION_MIN_Z;
import static dev.halfcraft.link.Proto.VOID_ALL;
import static dev.halfcraft.link.Proto.VOID_SOME;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertNotSame;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class HostCollisionTest {
	private static final long[] FULL = { -1L, -1L, -1L, -1L, -1L, -1L, -1L, -1L };
	private static final long B = COL_REGION_HEADER_BYTES;
	/** The first block of region (1, 0, 2), which {@link #header} describes. */
	private static final int PX = 8, PY = 0, PZ = 16;

	/** What the other test classes in this JVM find: no region known, no epoch joined. */
	@AfterEach
	void forgetTheCollision() {
		HostCollision.clear(-1);
	}

	/** The occupancy or void layers of a block: {@code head}, then zeros. */
	private static long[] layers(long... head) {
		return Arrays.copyOf(head, 8);
	}

	/** A ColRegion for region (1, 0, 2) in epoch 7, followed by {@code extra} zeroed bytes. */
	private static MemorySegment header(Arena arena, int count, long extra) {
		MemorySegment s = arena.allocate(B + extra, 8);
		s.set(JAVA_INT, COL_REGION_MIN_X, 8);
		s.set(JAVA_INT, COL_REGION_MIN_Z, 16);
		s.set(JAVA_INT, COL_REGION_MAX_X, 15);
		s.set(JAVA_INT, COL_REGION_MAX_Y, 7);
		s.set(JAVA_INT, COL_REGION_MAX_Z, 23);
		s.set(JAVA_INT, COL_REGION_EPOCH, 7);
		s.set(JAVA_INT, COL_REGION_COUNT, count);
		return s;
	}

	private static int index(int bx, int by, int bz) {
		return bx + 8 * bz + 64 * by;
	}

	@Test
	void shouldJoinOccupancyAndVoidLayerByLayer() {
		assertArrayEquals(layers(0xFFFF, -1), HostCollision.rock(layers(0xFF), layers(0xFF00, -1)));
	}

	@Test
	void shouldTakeEitherAloneAndGiveZerosForNeither() {
		long[] h = { 0, 0, 0, 0, -1L, -1L, -1L, -1L };
		assertArrayEquals(h, HostCollision.rock(null, h));
		assertArrayEquals(h, HostCollision.rock(h, null));
		assertArrayEquals(new long[8], HostCollision.rock(null, null));
	}

	@Test
	void shouldNeverChangeOrHandBackItsInputs() {
		long[] o = layers(1);
		long[] v = layers(2);
		assertNotSame(o, HostCollision.rock(o, v));
		assertNotSame(o, HostCollision.rock(o, null));
		assertNotSame(v, HostCollision.rock(null, v));
		assertArrayEquals(layers(1), o);
		assertArrayEquals(layers(2), v);
	}

	@Test
	void shouldReadAColVoidPayloadByBlockIndex() {
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment s = header(arena, 1, 512 + 64);
			s.set(JAVA_BYTE, B + index(0, 0, 0), (byte) VOID_ALL);
			s.set(JAVA_BYTE, B + index(1, 3, 2), (byte) VOID_SOME);
			for (int y = 0; y < 4; y++) {
				s.set(JAVA_LONG, B + 512 + y * 8L, -1L);
			}
			HostCollision.RegionVoid r = HostCollision.parseVoid(s, 0);
			assertArrayEquals(FULL, r.at(0, 0, 0));
			assertArrayEquals(layers(-1, -1, -1, -1), r.at(1, 3, 2));
			assertNull(r.at(2, 3, 1));
			assertNull(r.at(7, 7, 7));
		}
	}

	@Test
	void shouldHandOutOneSharedArrayForAWhollyVoidBlock() {
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment s = header(arena, 0, 512);
			s.set(JAVA_BYTE, B + index(0, 0, 0), (byte) VOID_ALL);
			s.set(JAVA_BYTE, B + index(5, 6, 7), (byte) VOID_ALL);
			HostCollision.RegionVoid r = HostCollision.parseVoid(s, 0);
			assertSame(r.at(0, 0, 0), r.at(5, 6, 7));
			assertSame(r.at(0, 0, 0), HostCollision.parseVoid(s, 0).at(5, 6, 7));
		}
	}

	@Test
	void shouldLeaveTheVoidSomeBlocksBeyondTheCountWithoutLayers() {
		try (Arena arena = Arena.ofConfined()) {
			// two blocks are partly void, the message holds the layers of one: the one with the greater index has none
			MemorySegment s = header(arena, 1, 512 + 64);
			s.set(JAVA_BYTE, B + index(5, 0, 0), (byte) VOID_SOME);
			s.set(JAVA_BYTE, B + index(1, 0, 1), (byte) VOID_SOME);
			s.set(JAVA_LONG, B + 512, 3L);
			HostCollision.RegionVoid r = HostCollision.parseVoid(s, 0);
			assertArrayEquals(layers(3), r.at(5, 0, 0));
			assertNull(r.at(1, 0, 1));
		}
	}

	@Test
	void shouldShowARegionsVoidOnceItsRegionArrivesAndForgetItWithTheRegion() {
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment voids = header(arena, 0, 512);
			voids.set(JAVA_BYTE, B, (byte) VOID_ALL);
			MemorySegment region = header(arena, 0, 0);

			HostCollision.clear(7);
			HostCollision.readVoid(voids, 0);
			assertNull(HostCollision.voidLayersAt(PX, PY, PZ));
			assertNull(HostCollision.rockLayersAt(PX, PY, PZ));

			HostCollision.readRegion(region, 0);
			assertArrayEquals(FULL, HostCollision.voidLayersAt(PX, PY, PZ));
			assertArrayEquals(FULL, HostCollision.rockLayersAt(PX, PY, PZ));
			assertNull(HostCollision.voidLayersAt(PX + 1, PY, PZ));
			assertArrayEquals(new long[8], HostCollision.rockLayersAt(PX + 1, PY, PZ));

			// sent again without a kColVoid before it: no void left in it
			HostCollision.readRegion(region, 0);
			assertNull(HostCollision.voidLayersAt(PX, PY, PZ));
			assertArrayEquals(new long[8], HostCollision.rockLayersAt(PX, PY, PZ));

			HostCollision.readVoid(voids, 0);
			HostCollision.readRegion(region, 0);
			assertArrayEquals(FULL, HostCollision.voidLayersAt(PX, PY, PZ));
			HostCollision.clear(8);
			assertNull(HostCollision.voidLayersAt(PX, PY, PZ));
			assertNull(HostCollision.rockLayersAt(PX, PY, PZ));
		}
	}

	@Test
	void shouldFindTheVoidOfNegativeBlockPositions() {
		try (Arena arena = Arena.ofConfined()) {
			// region (-1, -1, -1): blocks -8..-1 along every axis, the last of them is block 511
			MemorySegment voids = header(arena, 0, 512);
			MemorySegment region = header(arena, 0, 0);
			for (MemorySegment s : new MemorySegment[] { voids, region }) {
				s.set(JAVA_INT, COL_REGION_MIN_X, -8);
				s.set(JAVA_INT, COL_REGION_MIN_Y, -8);
				s.set(JAVA_INT, COL_REGION_MIN_Z, -8);
				s.set(JAVA_INT, COL_REGION_MAX_X, -1);
				s.set(JAVA_INT, COL_REGION_MAX_Y, -1);
				s.set(JAVA_INT, COL_REGION_MAX_Z, -1);
			}
			voids.set(JAVA_BYTE, B + 511, (byte) VOID_ALL);

			HostCollision.clear(7);
			HostCollision.readVoid(voids, 0);
			HostCollision.readRegion(region, 0);
			assertArrayEquals(FULL, HostCollision.voidLayersAt(-1, -1, -1));
			assertNull(HostCollision.voidLayersAt(-2, -1, -1));
			assertNull(HostCollision.voidLayersAt(-1, -1, 0));
		}
	}

	@Test
	void shouldForgetAVoidStillWaitingForItsRegionWhenCleared() {
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment voids = header(arena, 0, 512);
			voids.set(JAVA_BYTE, B, (byte) VOID_ALL);
			MemorySegment region = header(arena, 0, 0);

			HostCollision.clear(7);
			HostCollision.readVoid(voids, 0);
			HostCollision.clear(7);
			HostCollision.readRegion(region, 0);
			assertNull(HostCollision.voidLayersAt(PX, PY, PZ));
			assertArrayEquals(new long[8], HostCollision.rockLayersAt(PX, PY, PZ));
		}
	}

	@Test
	void shouldIgnoreAVoidFromAnotherEpoch() {
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment voids = header(arena, 0, 512);
			voids.set(JAVA_BYTE, B, (byte) VOID_ALL);
			MemorySegment region = header(arena, 0, 0);
			region.set(JAVA_INT, COL_REGION_EPOCH, 8);

			HostCollision.clear(8);
			HostCollision.readVoid(voids, 0);
			HostCollision.readRegion(region, 0);
			assertNull(HostCollision.voidLayersAt(PX, PY, PZ));
			assertArrayEquals(new long[8], HostCollision.rockLayersAt(PX, PY, PZ));
		}
	}
}
