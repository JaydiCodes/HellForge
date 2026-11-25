#include "shop/shop_internal.h"

/* Private implementation units share one translation unit to preserve the standalone mod's build interface.
 * Compile this entry point, not the included implementation files separately. */
#include "gameplay/game_inventory.c"
#include "currency/wallet_runtime.c"
#include "gameplay/loadout.c"
#include "gameplay/extra_slot.c"
#include "shop/ready_gate.c"
#include "gameplay/destroyer.c"
#include "gameplay/helldiver.c"
#include "gameplay/missions.c"
#include "currency/exchange.c"
#include "shop/purchases.c"
#include "shop/shop.c"

int SmrInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 16 || strcmp(api->build_id(), "25480438") != 0 ||
        api->size < offsetof(HfApi, global) + sizeof api->global || !api->code_matches || !api->global ||
        !api->on_game_thread || !api->read || !api->module_base || !api->logs_folder)
    {
        api->log(mod, "Resource shop requires SDK 16 and game build 25480438");
        return 1;
    }
    logs_folder = api->logs_folder(mod);
    if (!logs_folder || !logs_folder[0])
    {
        api->log(mod, "Cannot find the session logs folder.");
        return 1;
    }
    const char* names[PRICE_COUNT];
    unsigned prices[PRICE_COUNT];
    char detail[256];
    for (size_t i = 0; i < PRICE_COUNT; ++i) names[i] = kLoadoutPrices[i].name;
    if (!SmrLoadPrices(names, prices, PRICE_COUNT, detail, sizeof detail))
    {
        api->log(mod, "Cannot load prices.json: %s Shop not enabled.", detail);
        return 1;
    }
    for (size_t i = 0; i < PRICE_COUNT; ++i) kLoadoutPrices[i].requisition = prices[i];
    api->log(mod, "Loaded prices.json (%u items).", (unsigned)PRICE_COUNT);
    detail[0] = '\0';
    if (!SmrLoadUpgradeConfig(upgrade_tuning, detail, sizeof detail))
        api->log(mod, "Cannot load destroyer-upgrades.json: %s Destroyer upgrades disabled.", detail);
    else
        api->log(mod, "Loaded destroyer-upgrades.json.%s%s", detail[0] ? " " : "", detail);
    detail[0] = '\0';
    helldiver_config_loaded = SmrLoadHelldiverConfig(helldiver_tuning, detail, sizeof detail);
    if (!helldiver_config_loaded)
        api->log(mod, "Cannot load helldiver-upgrades.json: %s Helldiver upgrades disabled.", detail);
    else
        api->log(mod, "Loaded helldiver-upgrades.json.");
    if (hf_on_state(mod, UpgradeStateChanged, NULL, 0) ||
        (helldiver_config_loaded &&
            (hf_on_damage_pre(mod, HelldiverDamagePre, NULL, 0) || hf_on_weapon(mod, HelldiverWeapon, NULL, 0))) ||
        hf_on_mission_start(mod, MissionStart, NULL, 0) ||
        hf_on_mission_end(mod, MissionEnd, NULL, 0) ||
        hf_on_player_spawn(mod, PlayerSpawn, NULL, 0) ||
        hf_on_game_thread(mod, ShopTick, NULL))
        return 1;
    return 0;
}
