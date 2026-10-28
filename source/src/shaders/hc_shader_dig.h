#pragma once

// game_shader_generic_halfcraft.dll: client.dll's mask of the cells minecraft dug out of half-life's walls
// (shared/hc_dig_abi.h) for the dll's world shaders, whose pixel shaders leave those cells undrawn (DigClip in the sdk's
// stdshaders/hc_dig_clip_fxc.h): the mask in s15, its corner and layout in c20 and c21. the dll's project is valve's
// c++14 (no /std:c++17 there): no inline variables, no [[nodiscard]].

class CBaseShader;
class IShaderDynamicAPI;
class IShaderShadow;

namespace halfcraft
{
	struct DigMaskState;

	/// client.dll's dig mask (HalfCraft_DigMask), looked up until it's found (logged then). shaders draw on the material
	/// system's thread too.
	/// @return nullptr while client.dll exports none
	const DigMaskState* shader_dig_mask();

	/// a shader's shadow state: s15 is sampled
	void shader_dig_shadow(IShaderShadow* shadow);

	/// a shader's dynamic state: the mask in s15, c20 and c21 (DigMaskState). without a mask, or with one the material
	/// system marks an error texture, a black texture and zeros: nothing is clipped
	void shader_dig_dynamic(CBaseShader* shader, IShaderDynamicAPI* api);
}
