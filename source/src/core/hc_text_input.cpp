#include "hc_text_input.h"

#include "hc_log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace halfcraft
{
	namespace
	{
		HHOOK          g_hook = nullptr;
		bool           g_gave_up = false;
		wchar_t        g_high_surrogate = 0;
		std::u32string g_typed;

		void typed(wchar_t unit)
		{
			if (unit >= 0xD800 && unit <= 0xDBFF) {
				g_high_surrogate = unit;
				return;
			}
			char32_t ch = unit;
			if (unit >= 0xDC00 && unit <= 0xDFFF) {
				if (!g_high_surrogate) {
					return;
				}
				ch = 0x10000 + ((static_cast<char32_t>(g_high_surrogate) - 0xD800) << 10) + (unit - 0xDC00);
			}
			g_high_surrogate = 0;
			if (ch >= 0x20 && ch != 0x7F) {
				g_typed.push_back(ch);
			}
		}

		// source's message loop is ansi: by the time WM_CHAR reaches the window it went through the
		// ansi code page, and what that lacks (cyrillic on a western system, emoji) is a ?. a unicode
		// hook sees the message as it leaves the queue, still utf-16
		LRESULT CALLBACK message_hook(int code, WPARAM removed, LPARAM message)
		{
			if (code == HC_ACTION && removed == PM_REMOVE) {
				// a WH_GETMESSAGE hook gets its MSG's address as an LPARAM: windows' own contract
				const auto* msg = reinterpret_cast<const MSG*>(message);  // NOLINT(performance-no-int-to-ptr)
				if (msg->message == WM_CHAR) {
					typed(static_cast<wchar_t>(msg->wParam));
				}
			}
			return ::CallNextHookEx(g_hook, code, removed, message);
		}
	}

	void text_input_attach()
	{
		if (g_hook || g_gave_up) {
			return;
		}
		g_hook = ::SetWindowsHookExW(WH_GETMESSAGE, message_hook, nullptr, ::GetCurrentThreadId());
		if (!g_hook) {
			log_warning("text input: can't watch the game's messages (error %lu); minecraft's text fields get nothing typed", ::GetLastError());
			g_gave_up = true;
			return;
		}
		log_info("text input: watching the game's messages for typed characters");
	}

	void text_input_detach()
	{
		if (g_hook) {
			::UnhookWindowsHookEx(g_hook);
			g_hook = nullptr;
		}
		g_typed.clear();
	}

	std::u32string text_input_take()
	{
		std::u32string taken;
		taken.swap(g_typed);
		return taken;
	}
}
