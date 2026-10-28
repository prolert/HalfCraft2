#pragma once

// what client.dll and the shader dll (game_shader_generic_halfcraft.dll: the copy of LightmappedGeneric that leaves the
// cells minecraft dug out of half-life's walls undrawn) hand each other. both live in the same process: client.dll
// exports the dig mask and the shader dll looks it up by name. no sdk here and no inline variables: the shader dll's
// project builds this too.

class ITexture;

namespace halfcraft
{
	/// the dig mask as the pixel shader samples it (core/hc_dig_mask.h lays it out). its texture is client.dll's rgba
	/// image (bgra8888 in the texture, McTexture's): 4 bytes a cell, red 255 where the cell is dug.
	struct DigMaskState
	{
		ITexture* texture;    // s11: the mask's layers in tiles of 8 x 8 (nullptr: none yet, or no cell dug in it)
		float     origin[4];  // c20: (X0, Y1, Z0, 1 / 40), the mask's corner in source units
		float     layout[4];  // c21: (1 / 1024, 128, 8, w): w 0 off, 1 on, 2 every pixel in the mask cut unread (hc_debug_dig_clip 2)
	};

	/// client.dll -> the shader dll: the mask to clip by.
	/// @return nullptr while there's none
	using DigMaskFn = const DigMaskState* (*)();

	constexpr char HC_DIG_MASK_EXPORT[] = "HalfCraft_DigMask";

	/// the shader dll. loaded, it leaves the dug cells undrawn, so minecraft may dig them; without it (hl2's engine has
	/// none) digging is off
	constexpr char HC_SHADER_DLL[] = "game_shader_generic_halfcraft.dll";
}
