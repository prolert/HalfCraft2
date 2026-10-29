#pragma once

// server.dll: the players of a game. on a listen server the host, whose game the server runs in, is always the
// first slot (entity 1); guests take the slots after it. singleplayer has one slot, the host's.

class CBasePlayer;

namespace halfcraft
{
	/// the host's player: entity 1. nullptr on a dedicated server (nobody plays in it) or before the host spawned.
	CBasePlayer* host_player();

	/// the player is the host's (host_player()). false for nullptr.
	bool is_host(const CBasePlayer* player);

	/// how many player slots the server has (gpGlobals->maxClients): entities 1 to that are players.
	int player_slots();

	/// the player entity in a slot, while its client is connected (CBasePlayer::IsConnected: one that is
	/// disconnecting still is).
	/// @param index - its entity index, 1 to player_slots()
	/// @return nullptr for an empty slot (no player entity yet, or none any more) or a client gone
	CBasePlayer* connected_player(int index);

	/// fn(CBasePlayer&) for each connected player, in entity order from 1 to player_slots().
	template <class F>
	void for_each_player(F&& fn)
	{
		const int slots = player_slots();
		for (int index = 1; index <= slots; ++index) {
			if (CBasePlayer* player = connected_player(index)) {
				fn(*player);
			}
		}
	}
}
