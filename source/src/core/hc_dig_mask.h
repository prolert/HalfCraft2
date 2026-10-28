#pragma once

// where the dug cells go in the dig mask: the 128 x 64 x 128 cells around the player (x, y, z in minecraft blocks) that
// client.dll lays out in a 1024 x 1024 texture, which the wall shader clips by, and the constants the shader finds a cell
// with. pure code, no engine and no link.
//
// in client.dll, include it after the sdk's headers and tier0/valve_minmax_off.h (as source/src/client/hc_blocks.cpp does),
// which the sdk's min and max macros need.

#include <cmath>
#include <cstddef>
#include <cstdint>

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

		inline constexpr int SECTION_BLOCKS = 16;  // a section's blocks along each axis
		inline constexpr int TEXEL_BYTES = 4;      // rgba
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

		/// whether a section is in the layout. its edges lie on section borders (around()), so a section is in it whole or
		/// not at all.
		/// @param sx, sy, sz - the section (minecraft section coords: its blocks start at 16 times them)
		bool has_section(int sx, int sy, int sz) const
		{
			using dig_mask_detail::SECTION_BLOCKS;
			const int x0 = sx * SECTION_BLOCKS, y0 = sy * SECTION_BLOCKS, z0 = sz * SECTION_BLOCKS;
			const int last = SECTION_BLOCKS - 1;
			int       u = 0, v = 0;
			return texel(x0, y0, z0, u, v) && texel(x0 + last, y0 + last, z0 + last, u, v);
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

	/// the pixel shader's way to a cell's texel (DigClip, hc_dig_clip_fxc.h), in c++ to test it by: the cell is the
	/// distance from the mask's corner over 40, floored; its layer's tile and the cell in it give the texel.
	/// @param origin, layout - c20 and c21 (DigMaskLayout::shader_constants; layout's switch isn't read)
	/// @param p - a source point, already moved into the cell (the shader's half unit along the view)
	/// @param u, v - the texel the shader samples, when the cell is in the mask
	/// @return false when it isn't (u and v are left alone)
	inline bool shader_texel(const float origin[4], const float layout[4], const float p[3], int& u, int& v)
	{
		const float i = std::floor((p[0] - origin[0]) * origin[3]);
		const float j = std::floor((origin[1] - p[1]) * origin[3]);
		const float k = std::floor((p[2] - origin[2]) * origin[3]);
		if (i < 0.0f || j < 0.0f || k < 0.0f || i >= layout[1] || j >= layout[1] || k >= layout[1] * 0.5f) {
			return false;
		}
		const float row = std::floor(k / layout[2]);
		const float s = ((k - row * layout[2]) * layout[1] + i + 0.5f) * layout[0];
		const float t = (row * layout[1] + j + 0.5f) * layout[0];
		u = static_cast<int>(std::floor(s / layout[0]));
		v = static_cast<int>(std::floor(t / layout[0]));
		return true;
	}

	/// whether a texel of the mask says its cell is dug, as DigClip reads it: red without blue. a texture the material system
	/// may bind instead of the mask (its error texture's pink and black, white, grey, black) cuts nothing.
	/// @param red, blue - the texel's red and blue, 0..255 (the shader's 0..1 times 255)
	inline bool mask_texel_dug(std::uint8_t red, std::uint8_t blue)
	{
		return red >= 192 && blue <= 63;  // the shader's step( 0.75, r ) * step( b, 0.25 )
	}

	/// one section's cells into the mask's image: each of its 16 layers is a square of 16 x 16 texels, (255, 0, 0, 0) where
	/// a cell is dug (mask_texel_dug) and all zeros where not.
	/// @param sx, sy, sz - the section (minecraft section coords: its blocks start at 16 times them)
	/// @param bits - its 512 bytes, bit x + 16z + 256y; nullptr: nothing dug in it
	/// @param rgba - the image, DigMaskLayout::TEXTURE texels square
	/// @return false when the section isn't in the layout (nothing written)
	inline bool write_section(const DigMaskLayout& layout, int sx, int sy, int sz, const std::uint8_t* bits, std::uint8_t* rgba)
	{
		using dig_mask_detail::SECTION_BLOCKS;
		using dig_mask_detail::TEXEL_BYTES;
		if (!layout.has_section(sx, sy, sz)) {
			return false;
		}
		const int x0 = sx * SECTION_BLOCKS, y0 = sy * SECTION_BLOCKS, z0 = sz * SECTION_BLOCKS;
		int       u = 0, v = 0;
		for (int y = 0; y < SECTION_BLOCKS; ++y) {
			layout.texel(x0, y0 + y, z0, u, v);  // the layer's corner
			for (int z = 0; z < SECTION_BLOCKS; ++z) {
				std::uint8_t* texel = rgba + (static_cast<std::size_t>(v + z) * DigMaskLayout::TEXTURE + u) * TEXEL_BYTES;
				for (int x = 0; x < SECTION_BLOCKS; ++x, texel += TEXEL_BYTES) {
					const int  bit = x + SECTION_BLOCKS * z + SECTION_BLOCKS * SECTION_BLOCKS * y;
					const bool dug = bits && ((bits[bit >> 3] >> (bit & 7)) & 1) != 0;
					texel[0] = dug ? 255 : 0;
					texel[1] = texel[2] = texel[3] = 0;
				}
			}
		}
		return true;
	}

	/// whether the dig mask, placed around the player again, lays its image out afresh (cleared, the sections with cells dug
	/// that the new layout has written, the whole texture uploaded) or takes the new layout alone. an image with no texel
	/// dug that the new layout leaves with none is all zeros either way: on a map without holes, every 32 blocks of
	/// travel.
	/// @param built - the image and its texture exist (the first build always lays the image out: it makes the texture)
	/// @param image_dug - a texel of the image may be dug: a section with cells dug was written into it since the last
	/// rebuild that wrote none
	/// @param dug_in_layout - a section with cells dug lies in the new layout
	inline bool mask_needs_rebuild(bool built, bool image_dug, bool dug_in_layout)
	{
		return !built || image_dug || dug_in_layout;
	}

	/// whether the dig mask lays its image out afresh and uploads it whole before the shader clips by it this frame, instead
	/// of taking the sections that changed. the shader clips only while a cell is dug in the mask, and what the gpu holds of
	/// a mask it didn't clip by isn't known to be the image: nothing uploads it then (mask_needs_rebuild), and a level load
	/// or the material system restoring its resources may have changed it behind client.dll's back.
	/// @param dug_in_layout - a section with cells dug lies in the mask's layout
	/// @param clipping - the shader clipped by the mask the frame before, and nothing since may have changed the texture
	/// @param whole - this frame has laid the image out and uploaded it whole already (a recentre that rebuilt it)
	inline bool mask_needs_upload_before_clip(bool dug_in_layout, bool clipping, bool whole)
	{
		return dug_in_layout && !clipping && !whole;
	}
}
