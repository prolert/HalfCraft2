// client.dll: half-life's pickup history (CHudHistoryResource: the weapon, ammo and item icons down
// the right of the screen) goes to minecraft while its hud is up, which shows it as toasts (#26):
// "weapon <WeaponId>", "ammo <ammo type> <count>" or "item <classname>" on kStrPickup.

#include "cbase.h"

#include "ammodef.h"
#include "history_resource.h"
#include "hud.h"

#include "tier0/valve_minmax_off.h"
#include <string>

#include "client/hc_client.h"
#include "core/hc_log.h"
#include "shared/hc_hooks.h"
#include "shared/hc_weapon_ids.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		/// what minecraft reads for this pickup; "" for one it has nothing for
		std::string describe(int type, int id, C_BaseCombatWeapon* weapon, int count, CHudTexture* icon)
		{
			switch (type) {
			case HISTSLOT_WEAP: {
				const proto::WeaponId weapon_id = weapon ? halfcraft::weapon_id(weapon->GetName()) : proto::kHostWeaponNone;
				return weapon_id == proto::kHostWeaponNone ? std::string() : "weapon " + std::to_string(weapon_id);
			}
			case HISTSLOT_AMMO: {
				const Ammo_t* ammo = GetAmmoDef()->GetAmmoOfIndex(id);
				return ammo && ammo->pName ? std::string("ammo ") + ammo->pName + " " + std::to_string(count) : std::string();
			}
			case HISTSLOT_ITEM: return icon ? std::string("item ") + icon->szShortName : std::string();
			default: return {};  // ammo it couldn't take (full): source's red icon says that, minecraft doesn't
			}
		}
	}

	bool client_pickup(int type, int id, C_BaseCombatWeapon* weapon, int count, CHudTexture* icon)
	{
		auto& s = client_session();
		if (!s.link_ready || !s.link.mc_alive() || !s.minecraft_hud) {
			return false;
		}
		const std::string pickup = describe(type, id, weapon, count, icon);
		if (!pickup.empty() && !s.link.push_string(proto::kStrPickup, pickup)) {
			log_warning("pickups: the link is full; minecraft misses \"%s\"", pickup.c_str());
		}
		return true;
	}
}
