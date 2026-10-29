// client.dll: half-life's key hints ("%+use% OPEN DOOR", CHudHintKeyDisplay) go to minecraft, which
// shows them as toasts while its hud is up (#26). minecraft names the keys it has itself, from its
// own bindings. the ones source keeps (use, the flashlight, reload) become %sc:<sdl scancode>%,
// which minecraft names in the player's layout, and while source has the keyboard (vehicles,
// ladders) every key becomes %=<name>%, source's own binding, the way its hint panel names it.

#include "cbase.h"

#include "tier0/valve_minmax_off.h"
#include <string>

#include "client/hc_client.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		std::string   g_hint;          // the hint source shows next, utf-8, placeholders and all
		std::string   g_sent;          // what minecraft shows: "" for nothing
		std::uint32_t g_sent_epoch = 0;

		/// a key source keeps while minecraft has the keyboard (hc_input.cpp)
		ButtonCode_t source_key(const std::string& command)
		{
			if (!Q_stricmp(command.c_str(), "+use")) {
				return KEY_G;
			}
			if (!Q_stricmp(command.c_str(), "impulse 100")) {
				return KEY_V;
			}
			if (!Q_stricmp(command.c_str(), "+reload")) {
				return KEY_R;
			}
			return BUTTON_CODE_NONE;
		}

		std::string placeholder(const std::string& command, const ClientSession& s)
		{
			if (s.minecraft_owns_input) {
				const ButtonCode_t key = source_key(command);
				return key == BUTTON_CODE_NONE ? "%" + command + "%" : "%sc:" + std::to_string(sdl_scancode(key)) + "%";
			}
			// what CHudHintKeyDisplay::SetHintText does
			const char* key = engine->Key_LookupBinding(command[0] == '+' ? command.c_str() + 1 : command.c_str());
			char        name[64];
			Q_strncpy(name, key ? key : "< not bound >", sizeof(name));
			Q_strupr(name);
			return std::string("%=") + name + "%";
		}

		/// the hint with its placeholders as minecraft reads them
		std::string for_minecraft(const std::string& hint, const ClientSession& s)
		{
			std::string out;
			std::size_t at = 0;
			while (at < hint.size()) {
				const std::size_t open = hint.find('%', at);
				const std::size_t close = open == std::string::npos ? std::string::npos : hint.find('%', open + 1);
				if (close == std::string::npos) {
					out.append(hint, at, std::string::npos);
					break;
				}
				out.append(hint, at, open - at);
				out += placeholder(hint.substr(open + 1, close - open - 1), s);
				at = close + 1;
			}
			return out;
		}
	}

	void client_key_hint_text(const wchar_t* text)
	{
		char utf8[1024];
		Q_UnicodeToUTF8(text, utf8, sizeof(utf8));
		g_hint = utf8;
	}

	bool client_key_hint_drawn(bool shown)
	{
		auto&      s = client_session();
		const bool minecraft = s.link_ready && s.link.mc_alive() && s.minecraft_hud;
		if (g_sent_epoch != s.epoch) {
			g_sent_epoch = s.epoch;
			g_sent.clear();  // a minecraft (re)connected: it shows nothing yet
		}
		const std::string want = minecraft && shown ? for_minecraft(g_hint, s) : std::string();
		if (want != g_sent && s.link_ready && s.link.push_string(proto::kStrHint, want)) {
			g_sent = want;
		}
		return shown && !minecraft;
	}
}
