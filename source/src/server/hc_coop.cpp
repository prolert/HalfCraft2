// server.dll: co-op's fixes to valve's game code for players who live on. singleplayer reloads the map when the
// player dies; with two slots (the engine's deathmatch 1, never coop 1) respawn() spawns the dead player again
// instead, a path half-life 2's own code never took.

#include "cbase.h"
#include "player.h"

#include "tier0/valve_minmax_off.h"

#include "core/hc_log.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	void server_player_respawned(CBasePlayer* player)
	{
		if (player == nullptr) {
			return;
		}
		// CBasePlayer::Event_Killed made the body non-solid and Spawn() keeps it so (hl2mp's CHL2MP_Player::Spawn
		// clears it, hl2's never had to): such a player walks through the others and touches no trigger
		player->RemoveSolidFlags(FSOLID_NOT_SOLID);
		log_info("respawn: player %d is solid again", player->entindex());
	}
}
