package dev.halfcraft.world;

import com.mojang.serialization.Codec;
import com.mojang.serialization.codecs.RecordCodecBuilder;
import io.netty.buffer.ByteBuf;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.LongStream;
import net.minecraft.network.codec.ByteBufCodecs;
import net.minecraft.network.codec.StreamCodec;
import org.jspecify.annotations.Nullable;

/**
 * A chunk's cells dug out of Half-Life's walls, by section. Immutable: a change makes a new column (so
 * the chunk's attachment syncs). The maps lie apart along x, so the cells need no map of their own.
 */
public record DugColumn(List<DugSection> sections) {
	/** One section's dug cells (see {@link DugBits}); bits of any other length than 64 longs read as none. */
	public record DugSection(int sectionY, long[] bits) {
		static final Codec<DugSection> CODEC = RecordCodecBuilder.create(i -> i.group(
			Codec.INT.fieldOf("y").forGetter(DugSection::sectionY),
			Codec.LONG_STREAM.xmap(LongStream::toArray, Arrays::stream).fieldOf("bits").forGetter(DugSection::bits)
		).apply(i, DugSection::new));

		private boolean holds(int y) {
			return this.sectionY == y && this.bits.length == DugBits.LONGS;
		}
	}

	public static final DugColumn EMPTY = new DugColumn(List.of());
	public static final Codec<DugColumn> CODEC = DugSection.CODEC.listOf().xmap(DugColumn::new, DugColumn::sections);
	public static final StreamCodec<ByteBuf, DugColumn> STREAM_CODEC = new StreamCodec<>() {
		@Override
		public DugColumn decode(ByteBuf buf) {
			int n = ByteBufCodecs.VAR_INT.decode(buf);
			List<DugSection> sections = new ArrayList<>(n);
			for (int i = 0; i < n; i++) {
				int y = ByteBufCodecs.VAR_INT.decode(buf);
				long[] bits = new long[DugBits.LONGS];
				for (int k = 0; k < DugBits.LONGS; k++) {
					bits[k] = buf.readLong();
				}
				sections.add(new DugSection(y, bits));
			}
			return new DugColumn(sections);
		}

		@Override
		public void encode(ByteBuf buf, DugColumn column) {
			ByteBufCodecs.VAR_INT.encode(buf, column.sections.size());
			for (DugSection s : column.sections) {
				ByteBufCodecs.VAR_INT.encode(buf, s.sectionY);
				for (int k = 0; k < DugBits.LONGS; k++) {
					buf.writeLong(s.bits.length == DugBits.LONGS ? s.bits[k] : 0L);
				}
			}
		}
	};

	public DugColumn {
		sections = List.copyOf(sections);
	}

	/** The dug cells of section {@code sectionY}, or null for none. Never change them. */
	public long @Nullable [] bits(int sectionY) {
		for (DugSection s : this.sections) {
			if (s.holds(sectionY)) {
				return s.bits;
			}
		}
		return null;
	}

	public boolean has(int x, int y, int z) {
		long[] bits = bits(y >> 4);
		return bits != null && DugBits.get(bits, DugBits.index(x, y, z));
	}

	/** This column with the cell (x, y, z) dug too; this one stays as it is. */
	public DugColumn with(int x, int y, int z) {
		int sy = y >> 4;
		int bit = DugBits.index(x, y, z);
		List<DugSection> out = new ArrayList<>(this.sections.size() + 1);
		boolean found = false;
		for (DugSection s : this.sections) {
			if (!found && s.holds(sy)) {
				long[] bits = s.bits.clone();
				DugBits.set(bits, bit);
				out.add(new DugSection(sy, bits));
				found = true;
			} else {
				out.add(s);
			}
		}
		if (!found) {
			long[] bits = new long[DugBits.LONGS];
			DugBits.set(bits, bit);
			out.add(new DugSection(sy, bits));
		}
		return new DugColumn(out);
	}
}
