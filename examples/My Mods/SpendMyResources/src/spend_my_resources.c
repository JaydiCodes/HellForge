#include "shop_internal.h"

/* Private implementation units share one translation unit to preserve the standalone mod's build interface.
 * Compile this entry point, not the included implementation files separately. */
#include "game_inventory.c"
#include "loadout.c"
#include "ready_gate.c"
#include "missions.c"
#include "purchases.c"
#include "shop.c"

int SmrInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 16 || strcmp(api->build_id(), "25480438") != 0 ||
        api->size < offsetof(HfApi, global) + sizeof api->global || !api->code_matches || !api->global ||
        !api->on_game_thread || !api->read || !api->module_base || !api->add_weapon || !api->logs_folder)
    {
        api->log(mod, "Resource shop requires SDK 16 and game build 25480438");
        return 1;
    }
    HfWeapon token = {0};
    token.size = sizeof token;
    token.base = HF_ITEM_AR23A_LIBERATOR_CARBINE;
    token.unit = "jaydicodes/spend_my_resources/access_token";
    token_weapon = api->add_weapon(mod, &token);
    if (!token_weapon)
    {
        api->log(mod, "Cannot register the purchase token.");
        return 1;
    }
    uint32_t hash = 245428325u * 0x5BD1E995u;
    hash ^= hash >> 24;
    token_attachment = hash * 0x5BD1E995u ^ token_weapon * 0x5BD1E995u;
    logs_folder = api->logs_folder(mod);
    if (!logs_folder || !logs_folder[0])
    {
        api->log(mod, "Cannot find the session logs folder.");
        return 1;
    }
    if (hf_on_mission_start(mod, MissionStart, NULL, 0) ||
        hf_on_mission_end(mod, MissionEnd, NULL, 0) ||
        hf_on_player_spawn(mod, PlayerSpawn, NULL, 0) ||
        hf_on_game_thread(mod, ShopTick, NULL))
        return 1;
    return 0;
}
