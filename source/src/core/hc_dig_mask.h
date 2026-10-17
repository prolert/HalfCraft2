#pragma once

// where the dug cells go in the dig mask: the 128 x 64 x 128 cells around the player (x, y, z in minecraft blocks) that
// client.dll lays out in a 1024 x 1024 texture, which the wall shader clips by, and the constants the shader finds a cell
// with. pure code, no engine and no link.
//
// in client.dll, include it after the sdk's headers and tier0/valve_minmax_off.h (as source/src/client/hc_blocks.cpp does),
// which the sdk's min and max macros need.

#include <cmath>

#include "hc_units.h"

namespace halfcraft
{
	namespace dig_mask_detail
	{
		/// down to a multiple of 16
		inline int floor16(double v)
		{
			return static_cast<int>(std::floor(v / 16.0)) * 16;
		}
	}

	/// the layers, one for each y, lie in tiles of 8 x 8 in the texture; a dug cell's texel has red 255 (the pixel shader reads
	/// it). (ox, oy, oz) is the block of texel (0, 0): the layout's lowest corner.
	struct DigMaskLayout
	{
		static constexpr int SIZE_XZ = 128;   // cells along x and along z
		static constexpr int SIZE_Y = 64;     // layers
		static constexpr int TILES = 8;       // layers along a row of tiles, and along a column
		static constexpr int TEXTURE = 1024;  // texels along the texture's edge
		static_assert(SIZE_XZ * TILES == TEXTURE && TILES * TILES == SIZE_Y, "the tiles fill the texture");

		static constexpr int CENTRE_XZ = SIZE_XZ / 2;  // cells from (ox, oz) to the centre
		static constexpr int CENTRE_Y = SIZE_Y / 2;    // layers from oy to the centre

		static constexpr double RECENTRE_XZ = 32.0;  // blocks from the centre along x or z (see needs_recentre)
		static constexpr double RECENTRE_Y = 16.0;   // blocks from the centre along y

		int ox = 0, oy = 0, oz = 0;

		/// the layout around a position: (ox, oy, oz) = floor16(x, y, z) - (64, 32, 64), where floor16 goes down to a multiple
		/// of 16, so the layout's edges lie on section borders.
		static DigMaskLayout around(double x, double y, double z)
		{
			DigMaskLayout layout;
			layout.ox = dig_mask_detail::floor16(x) - CENTRE_XZ;
			layout.oy = dig_mask_detail::floor16(y) - CENTRE_Y;
			layout.oz = dig_mask_detail::floor16(z) - CENTRE_XZ;
			return layout;
		}

		/// whether a position is further from the centre (ox + 64, oy + 32, oz + 64) than 32 blocks along x or z, or 16 along y:
		/// the layout is to be placed around it again
		bool needs_recentre(double x, double y, double z) const
		{
			return std::fabs(x - (ox + CENTRE_XZ)) > RECENTRE_XZ || std::fabs(z - (oz + CENTRE_XZ)) > RECENTRE_XZ || std::fabs(y - (oy + CENTRE_Y)) > RECENTRE_Y;
		}

		/// the texel of a block's cell: with i, j, k = x - ox, z - oz, y - oy, u = (k % 8) * 128 + i, v = (k / 8) * 128 + j
		/// @param u, v - the texel, when the block is in the layout
		/// @return false when it isn't (u and v are left alone)
		bool texel(int x, int y, int z, int& u, int& v) const
		{
			const int i = x - ox, j = z - oz, k = y - oy;
			if (i < 0 || i >= SIZE_XZ || j < 0 || j >= SIZE_XZ || k < 0 || k >= SIZE_Y) {
				return false;
			}
			u = (k % TILES) * SIZE_XZ + i;
			v = (k / TILES) * SIZE_XZ + j;
			return true;
		}

		/// what the pixel shader (c20 and c21) needs to find the cell of a source point p, in a map's slot:
		/// the cell is (floor((p.x - X0) / 40), floor((Y1 - p.y) / 40), floor((p.z - Z0) / 40)), its texel texel()'s of it.
		/// @param origin - c20: {X0 = (ox - x_blocks) * 40, Y1 = -oz * 40, Z0 = oy * 40 + grid_z, 1 / 40}
		/// @param layout - c21: {1 / 1024, 128, 8, 1}; the last is the mask's switch, which 0 turns off
		void shader_constants(MapSlot slot, float origin[4], float layout[4]) const
		{
			origin[0] = static_cast<float>((ox - slot.x_blocks()) * UNITS_PER_BLOCK);
			origin[1] = static_cast<float>(-oz * UNITS_PER_BLOCK);
			origin[2] = static_cast<float>(oy * UNITS_PER_BLOCK + slot.grid_z);
			origin[3] = static_cast<float>(1.0 / UNITS_PER_BLOCK);
			layout[0] = 1.0f / static_cast<float>(TEXTURE);
			layout[1] = static_cast<float>(SIZE_XZ);
			layout[2] = static_cast<float>(TILES);
			layout[3] = 1.0f;
		}
	};
}
