#pragma once

// client.dll: the cells minecraft dug out of half-life's walls. minecraft sends each section's mask with the section
// (proto::kRenDug); the ones of the current map's slot are kept here, for server.dll (HalfCraft_DugSince, which cuts
// them out of the collision it streams), for source's own player movement (box_dug, shared/hc_hooks.h) and, laid out
// around the player in a mask texture (core/hc_dig_mask.h), for the shader dll (HalfCraft_DigMask, shared/hc_dig_abi.h),
// which leaves them undrawn. none of it goes anywhere without the shader dll: half-life's walls are whole then, in
// source and in minecraft.

#include <cstdint>

#include "core/hc_units.h"

namespace halfcraft
{
	struct DigMaskState;
	struct DugSection;

	/// one render-ring message (main thread): kRenDug, and kRenClearAll, after which minecraft sends every section again,
	/// the dug ones first: the sections with cells dug stay as they are until their kRenDug comes, 2 s at most
	/// (core/hc_dug_cells.h's DugPending; dig_update empties the ones that didn't come).
	void dig_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes);

	/// the current map's slot; empties the sections that belong to another map's slot (as blocks_set_slot), and the ones
	/// a kRenClearAll left pending: another map ends their grace.
	void dig_set_slot(MapSlot slot);

	/// whether the shader dll is loaded (looked at each call; logged the first time and when it changes).
	bool dig_shader_ready();

	/// whether minecraft may dig new cells: hc_dig, and the shader dll (proto::kHostDigOff otherwise).
	bool dig_enabled();

	/// once per frame, after the frame's messages. the sections a kRenClearAll left pending that minecraft hasn't sent
	/// again within 2 s are emptied first. with the shader dll, the dig mask takes the sections that changed (placed
	/// around the player when it's new or the player went too far from its centre: laid out afresh and uploaded then,
	/// unless no cell is dug in it before or after, core/hc_dig_mask.h's mask_needs_rebuild). while a cell is dug in the
	/// mask (the holes the shader dll cuts out of the walls of the map being played, whatever hc_dig says), r_novis 1
	/// and r_portalsopenall 1: the rooms behind the holes are drawn whatever the map's vis says. their old values come
	/// back once no hole is in the mask (holes further away are drawn closed), and off the map.
	/// @param player - the player's feet, minecraft coords
	/// @param in_game - a map is being played (HalfCraftClientSystem::update's)
	void dig_update(const McVec& player, bool in_game);

	/// whether dig_update found a hole in the dig mask (r_novis is 1 for it): what's dug in the rock behind the map's
	/// walls can be seen then (hc_blocks.cpp draws its sections, client_draw_unplaced_blocks).
	bool dig_holes_near();

	/// gives r_novis and r_portalsopenall their old values back, frees the dig mask and drops every section (client
	/// shutdown).
	void dig_shutdown();

	/// the dig mask for the shader dll (HalfCraft_DigMask): all zeros (no texture, switched off) without the dll
	const DigMaskState* dig_mask_state();

	/// sections whose dug cells changed, for server.dll (see shared/hc_bridge.h, DugSinceFn). without the shader dll
	/// none (now is left alone): what half-life draws stays solid for minecraft too.
	int dug_since(std::uint32_t since, DugSection* out, int max, std::uint32_t* now);
}
