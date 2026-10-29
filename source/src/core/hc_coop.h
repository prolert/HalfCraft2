#pragma once

// co-op hosting: the console commands client.dll's hc_host runs to host co-op for two. a two-slot listen server
// on a map, with coop 0: coop 1 breaks every map change after it (the engine then starts no new map), and with two
// slots the engine sets deathmatch 1 itself, which is what makes a dead player respawn instead of the map reload.
// sv_lan 1 keeps the server to the lan; a vpn (radmin, hamachi) needs sv_lan 0. pure: no windows, no sdk.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace halfcraft
{
	// the longest map name hc_host takes: well within the engine's buffers (the sdk's MAX_MAP_NAME, 96, holds
	// maps/<name>.bsp), and far longer than any of the game's maps
	inline constexpr std::size_t MAX_HOSTED_MAP_NAME = 64;

	/// one of A-Z, a-z, 0-9 and _: what hc_host takes a map name to be made of.
	inline bool map_name_character(char c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
	}

	/// a map name hc_host takes: 1 to MAX_HOSTED_MAP_NAME characters, each a map_name_character. the name goes into
	/// the console as it is, so nothing else may: a ; or a line break would run a command after it.
	inline bool is_map_name(std::string_view name)
	{
		return !name.empty() && name.size() <= MAX_HOSTED_MAP_NAME && std::all_of(name.begin(), name.end(), map_name_character);
	}

	/// the console commands that host co-op for two on a map: drop the game there is, take two slots, keep to the lan
	/// (or not, for a vpn), no coop, and load the map. the engine refuses maxplayers while a server runs ("cannot
	/// change maxplayers while the server is running"), and disconnect only asks for the game's shutdown, which the
	/// engine does at the start of its next frame: the wait holds the rest of the line until then. without it, hosting
	/// from a game of one slot would load the map with one slot again.
	/// @param map - the map's name, without maps/ and .bsp
	/// @param vpn - guests come over a vpn: sv_lan 0
	/// @return empty when map isn't a map name
	inline std::string host_commands(std::string_view map, bool vpn)
	{
		if (!is_map_name(map)) {
			return {};
		}
		return std::string("disconnect; wait; maxplayers 2; sv_lan ") + (vpn ? "0" : "1") + "; coop 0; map " + std::string(map);
	}
}
