// game_shader_generic_halfcraft.dll: client.dll's mask of the cells minecraft dug, for the world shaders (see
// hc_shader_dig.h).

#include "shaderlib/cshader.h"
#include "tier0/dbg.h"

#include <atomic>

#include "core/hc_module.h"
#include "shaders/hc_shader_dig.h"
#include "shared/hc_dig_abi.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr Sampler_t DIG_MASK_SAMPLER = SHADER_SAMPLER15;  // DigMaskSampler
		constexpr int       DIG_ORIGIN_REGISTER = 20;             // c20, g_DigOrigin
		constexpr int       DIG_LAYOUT_REGISTER = 21;             // c21, g_DigLayout

		std::atomic<DigMaskFn> g_dig_mask{ nullptr };
	}

	const DigMaskState* shader_dig_mask()
	{
		DigMaskFn mask = g_dig_mask.load(std::memory_order_acquire);
		if (!mask) {
			mask = reinterpret_cast<DigMaskFn>(find_export("client.dll", HC_DIG_MASK_EXPORT));
			if (!mask) {
				return nullptr;
			}
			if (!g_dig_mask.exchange(mask, std::memory_order_acq_rel)) {  // one line, whichever thread found it first
				Msg("[halfcraft] the world shader reads client.dll's dig mask\n");
			}
		}
		return mask();
	}

	void shader_dig_shadow(IShaderShadow* shadow)
	{
		shadow->EnableTexture(DIG_MASK_SAMPLER, true);
	}

	void shader_dig_dynamic(CBaseShader* shader, IShaderDynamicAPI* api)
	{
		const DigMaskState* state = shader_dig_mask();
		if (state && state->texture) {
			shader->BindTexture(DIG_MASK_SAMPLER, state->texture);
			api->SetPixelShaderConstant(DIG_ORIGIN_REGISTER, state->origin, 1);
			api->SetPixelShaderConstant(DIG_LAYOUT_REGISTER, state->layout, 1);
			return;
		}
		static const float zero[4] = {};
		api->BindStandardTexture(DIG_MASK_SAMPLER, TEXTURE_BLACK);
		api->SetPixelShaderConstant(DIG_ORIGIN_REGISTER, zero, 1);
		api->SetPixelShaderConstant(DIG_LAYOUT_REGISTER, zero, 1);
	}
}
