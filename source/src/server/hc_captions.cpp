// server.dll: who says a close caption. the caption the client gets has only its token, so the
// server tells client.dll the speaker as it sends one (hc_bridge.h), and minecraft's chat names them
// ("<Alyx> Come on, Gordon."). the model tells apart who share a class (odessa is a citizen).

#include "cbase.h"

#include "shared/hc_bridge.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	void server_caption_speaker(const char* token, CBaseEntity* actor)
	{
		static CaptionSpeakerFn caption_speaker = nullptr;
		if (!caption_speaker) {
			caption_speaker = reinterpret_cast<CaptionSpeakerFn>(find_export("client.dll", HC_CAPTION_SPEAKER_EXPORT));
		}
		if (!caption_speaker || !actor) {
			return;
		}
		char model[MAX_PATH];
		V_FileBase(STRING(actor->GetModelName()), model, sizeof(model));
		char speaker[2 * MAX_PATH];
		V_snprintf(speaker, sizeof(speaker), "%s/%s", actor->GetClassname(), model);
		V_strlower(speaker);
		caption_speaker(token, speaker);
	}
}
