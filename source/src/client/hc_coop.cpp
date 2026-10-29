// client.dll: hc_host, the host's setup for co-op (core/hc_coop.h). it runs the console commands that turn this
// game into a listen server for two on a map, which a guest joins with connect <this pc's lan or vpn address>.
// co-op runs on half-life 2: deathmatch's engine only (hl2dm, 64-bit): half-life 2's own (hl2, 32-bit) refuses it.

#include "cbase.h"

#include "tier0/valve_minmax_off.h"
#include <string>

#include "core/hc_coop.h"
#include "core/hc_log.h"
#include "core/hc_units.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CON_COMMAND(hc_host, "halfcraft: host co-op for two: hc_host [map] [vpn] (a listen server for 2 on that map, sv_lan 0 with vpn; without a map, the current one)")
{
#if !defined(_WIN64)
	(void)args;
	halfcraft::log_info("hc_host: co-op needs half-life 2: deathmatch's engine (hl2dm)");
#else
	// hc_host [map] [vpn]; a lone vpn is the flag, on the current map
	int         next = 1;
	std::string map;
	if (next < args.ArgC() && Q_stricmp(args[next], "vpn") != 0) {
		map = args[next++];
	}
	const bool vpn = next < args.ArgC() && Q_stricmp(args[next], "vpn") == 0;
	if (vpn) {
		++next;
	}
	// without a map, the one this game is on; the menu's background is a map too, so its level name isn't empty
	if (map.empty() && engine->IsInGame() && !engine->IsLevelMainMenuBackground()) {
		map = halfcraft::map_base_name(engine->GetLevelName());
	}
	const std::string commands = next == args.ArgC() ? halfcraft::host_commands(map, vpn) : std::string();
	if (commands.empty()) {
		halfcraft::log_info("hc_host: hc_host <map> [vpn] hosts co-op for two on that map (sv_lan 0 with vpn), or on the current one without a map");
	} else {
		halfcraft::log_info("hc_host: co-op for two on %s%s: %s", map.c_str(), vpn ? " over a vpn" : "", commands.c_str());
		engine->ClientCmd_Unrestricted(commands.c_str());
	}
	halfcraft::log_info("hc_host: guests connect to this pc's lan or vpn address (status shows it), never 127.0.0.1");
#endif
}
