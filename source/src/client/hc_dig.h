#pragma once

// client.dll: the cells minecraft dug out of half-life's walls. minecraft sends each section's mask with the section
// (proto::kRenDug); the ones of the current map's slot are kept here, for server.dll (HalfCraft_DugSince, which cuts
// them out of the collision it streams) and for source's own player movement (box_dug, shared/hc_hooks.h). none of
// it goes anywhere without the shader dll (shared/hc_dig_abi.h), which leaves them undrawn: half-life's walls are
// whole then, in source and in minecraft.

#include <cstdint>

#include "core/hc_units.h"

namespace halfcraft
{
	struct DugSection;

	/// one render-ring message (main thread): kRenDug, and kRenClearAll, which empties every section.
	void dig_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes);

	/// the current map's slot; empties the sections that belong to another map's slot (as blocks_set_slot).
	void dig_set_slot(MapSlot slot);

	/// whether the shader dll is loaded (looked at each call; logged the first time and when it changes).
	bool dig_shader_ready();

	/// whether minecraft may dig new cells: hc_dig, and the shader dll (proto::kHostDigOff otherwise).
	bool dig_enabled();

	/// once per frame. while the map being played has holes and the shader dll cuts them out of its walls (whatever
	/// hc_dig says), r_novis 1 and r_portalsopenall 1: the rooms behind the holes are drawn whatever the map's vis
	/// says. their old values come back on a map without holes, and off the map.
	/// @param player - where minecraft's player stands, minecraft coords
	/// @param in_game - a map is being played (HalfCraftClientSystem::update's)
	void dig_update(const McVec& player, bool in_game);

	/// gives r_novis and r_portalsopenall their old values back and drops every section (client shutdown).
	void dig_shutdown();

	/// sections whose dug cells changed, for server.dll (see shared/hc_bridge.h, DugSinceFn). without the shader dll
	/// none (now is left alone): what half-life draws stays solid for minecraft too.
	int dug_since(std::uint32_t since, DugSection* out, int max, std::uint32_t* now);
}
