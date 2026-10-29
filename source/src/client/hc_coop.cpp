// client.dll: hc_host, the host's setup for co-op (core/hc_coop.h). it runs the console commands that turn this
// game into a listen server for two on a map, which a guest joins with connect <this pc's lan or vpn address>.
// co-op runs on half-life 2: deathmatch's engine only (hl2dm, 64-bit): half-life 2's own (hl2, 32-bit) refuses it.
//
// the engine refuses maxplayers while a server runs, and disconnect only asks for the server's shutdown, which comes
// at the start of a later frame. the wait in host_commands isn't enough for that: it holds the rest of the line for
// one pass of the command buffer, and the buffer runs more than once a frame (typed from a game, 2 of 5 such lines
// still found the server up). so with a server up, hc_host disconnects first and runs host_commands once server.dll
// says its level is gone (hc_bridge.h).

#include "cbase.h"
#include "filesystem.h"

#include "tier0/valve_minmax_off.h"
#include <string>
#include <string_view>
#include <vector>

#include "core/hc_coop.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "core/hc_units.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined(_WIN64)
namespace halfcraft
{
	namespace
	{
		constexpr double STOP_SECONDS = 10.0;  // how long hc_host waits for the server there was to stop

		/// whether this process's server has a level (server.dll's HalfCraft_ServerInLevel); false when server.dll
		/// can't say, and hc_host then runs its line at once, as it would at the menu.
		bool server_in_level()
		{
			static ServerInLevelFn in_level = nullptr;
			if (!in_level) {
				in_level = reinterpret_cast<ServerInLevelFn>(find_export("server.dll", HC_SERVER_IN_LEVEL_EXPORT));
			}
			return in_level && in_level() != 0;
		}

		// what hc_host started, until the level it asked for comes up
		struct Hosting
		{
			std::string map;       // lowercase, as the level's name comes (empty: none)
			std::string commands;  // host_commands, while the server there was still has its level (empty: run)
			double      give_up = 0.0;
		};

		Hosting g_hosting;

		class HostSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HostSystem() : CAutoGameSystemPerFrame("HalfCraftHost") {}

			// the server there was stopped: now maxplayers is taken
			void Update(float) override
			{
				if (g_hosting.commands.empty()) {
					return;
				}
				if (!server_in_level()) {
					engine->ClientCmd_Unrestricted(g_hosting.commands.c_str());
					g_hosting.commands.clear();
				} else if (Plat_FloatTime() > g_hosting.give_up) {
					log_warning("hc_host: the game there was didn't stop in %.0f s: nothing hosted", STOP_SECONDS);
					g_hosting = {};
				}
			}

			// the level hc_host started says whether it has the slots co-op needs: had the engine refused maxplayers
			// after all, it would say so only in a line of its own before the map loaded with the old count
			void LevelInitPostEntity() override
			{
				if (g_hosting.map.empty() || !g_hosting.commands.empty()) {
					return;
				}
				const std::string hosted = g_hosting.map;
				g_hosting = {};
				const std::string level = map_base_name(engine->GetLevelName());
				const int         slots = engine->GetMaxClients();
				if (level != hosted) {
					log_warning("hc_host: %s came up, not %s, which hc_host started", level.c_str(), hosted.c_str());
				} else if (!hosted_level_has_room(slots)) {
					log_warning("hc_host: %s came up with %d slot%s, not %d: no guest can join (the engine kept the maxplayers it "
								"had); hc_host again from the menu",
						level.c_str(), slots, slots == 1 ? "" : "s", HOST_SLOTS);
				} else {
					log_info("hc_host: %s is up with %d slots", level.c_str(), slots);
				}
			}
		};

		HostSystem g_host_system;

		/// host co-op on a map: at once, or after the server there is has stopped.
		void host(const HostRequest& request)
		{
			const std::string commands = host_commands(request.map, request.vpn);
			const bool        stop_first = server_in_level();
			log_info("hc_host: starting %s for two%s%s: %s", request.map.c_str(), request.vpn ? " over a vpn" : "",
				stop_first ? ", once the game there is stops" : "", commands.c_str());
			g_hosting = { map_base_name(request.map.c_str()), stop_first ? commands : std::string(), Plat_FloatTime() + STOP_SECONDS };
			engine->ClientCmd_Unrestricted(stop_first ? "disconnect" : commands.c_str());
		}
	}
}
#endif

CON_COMMAND(hc_host, "halfcraft: host co-op for two: hc_host [map] [vpn] (a listen server for 2 on that map, sv_lan 0 with vpn; without a map, the current one)")
{
#if !defined(_WIN64)
	(void)args;
	halfcraft::log_info("hc_host: co-op needs half-life 2: deathmatch's engine (hl2dm)");
#else
	std::vector<std::string_view> words;
	for (int i = 1; i < args.ArgC(); ++i) {
		words.emplace_back(args[i]);
	}
	// the menu's background is a map too, so its level name isn't empty
	const bool        in_game = engine->IsInGame() && !engine->IsLevelMainMenuBackground();
	const std::string current = in_game ? halfcraft::map_base_name(engine->GetLevelName()) : std::string();
	const auto        request = halfcraft::parse_host_args(words, current);
	if (!request) {
		halfcraft::log_info("hc_host: hc_host <map> [vpn] hosts co-op for two on that map (sv_lan 0 with vpn), or on the current one without a map");
	} else if (const std::string file = halfcraft::map_file(request->map); !filesystem->FileExists(file.c_str(), "GAME")) {
		// before disconnect: a typo leaves the game as it is
		halfcraft::log_info("hc_host: there's no %s; nothing changed", file.c_str());
	} else {
		halfcraft::host(*request);
	}
	halfcraft::log_info("hc_host: guests connect to this pc's lan or vpn address (status shows it), never 127.0.0.1");
#endif
}
