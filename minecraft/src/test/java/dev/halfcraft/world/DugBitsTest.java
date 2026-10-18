package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.JsonElement;
import com.mojang.serialization.JsonOps;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import java.util.List;
import org.junit.jupiter.api.Test;

class DugBitsTest {
	@Test
	void shouldIndexXThenZThenY() {
		assertEquals(0, DugBits.index(0, 0, 0));
		assertEquals(1, DugBits.index(1, 0, 0));
		assertEquals(16, DugBits.index(0, 0, 1));
		assertEquals(256, DugBits.index(0, 1, 0));
		assertEquals(4095, DugBits.index(15, 15, 15));
		// the cell within its section, whatever section it's in
		assertEquals(4095, DugBits.index(-1, -1, -1));
		assertEquals(273, DugBits.index(17, 33, -15));
	}

	@Test
	void shouldSetGetAndCountBits() {
		long[] bits = new long[64];
		DugBits.set(bits, 0);
		DugBits.set(bits, 4095);
		DugBits.set(bits, 4095);
		assertTrue(DugBits.get(bits, 0));
		assertTrue(DugBits.get(bits, 4095));
		assertFalse(DugBits.get(bits, 1));
		assertEquals(2, DugBits.count(bits));
	}

	@Test
	void shouldWriteBytesAsLittleEndianWords() {
		long[] bits = new long[64];
		DugBits.set(bits, 0);
		DugBits.set(bits, 9);
		DugBits.set(bits, 4095);
		byte[] bytes = DugBits.toBytes(bits);
		assertEquals(512, bytes.length);
		assertEquals(1, bytes[0]);
		assertEquals(2, bytes[1]);
		assertEquals((byte) 0x80, bytes[511]);
		int nonzero = 0;
		for (byte b : bytes) {
			if (b != 0) {
				nonzero++;
			}
		}
		assertEquals(3, nonzero);
	}

	@Test
	void shouldMarkCellsInANewColumnAndLeaveTheOldOne() {
		DugColumn one = DugColumn.EMPTY.with(1, 2, 3);
		assertTrue(one.has(1, 2, 3));
		assertFalse(one.has(1, 2, 4));
		assertFalse(DugColumn.EMPTY.has(1, 2, 3));

		DugColumn two = one.with(1, -5, 3).with(2, 2, 3);
		assertEquals(1, one.sections().size());
		assertEquals(2, two.sections().size());
		assertFalse(one.has(2, 2, 3));
		assertNotNull(two.bits(-1));
		assertNull(two.bits(5));
		long[] section = two.bits(0);
		assertNotNull(section);
		assertEquals(2, DugBits.count(section));
	}

	@Test
	void shouldKeepCellsThroughBothCodecs() {
		DugColumn column = DugColumn.EMPTY.with(1, 2, 3).with(4, -20, 5);

		JsonElement json = DugColumn.CODEC.encodeStart(JsonOps.INSTANCE, column).getOrThrow();
		DugColumn fromJson = DugColumn.CODEC.parse(JsonOps.INSTANCE, json).getOrThrow();

		ByteBuf buf = Unpooled.buffer();
		DugColumn.STREAM_CODEC.encode(buf, column);
		DugColumn fromStream = DugColumn.STREAM_CODEC.decode(buf);
		assertEquals(0, buf.readableBytes());

		for (DugColumn back : List.of(fromJson, fromStream)) {
			assertTrue(back.has(1, 2, 3));
			assertTrue(back.has(4, -20, 5));
			assertFalse(back.has(1, 2, 4));
		}
	}
}
