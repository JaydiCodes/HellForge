#include "hellforge_mod.h"
#include <stdio.h>
#include <string.h>

#define BLUNT_FORCE_MITIGATION 40u

static void CheckPassives(HfMod* mod, float dt, void* user)
{
    static float elapsed = 2.0f;
    static char previous[256];
    char status[256];
    HfPassives passives;
    const HfApi* api = HF_API(mod);

    elapsed += dt;
    if (elapsed < 2.0f) return;
    elapsed = 0.0f;

    int readable = api->get_passives(mod, &passives);
    if (passives.want_armor != HF_PASSIVE_KIT || passives.want_second != BLUNT_FORCE_MITIGATION)
        snprintf(status, sizeof status, "override changed by another caller (armor %u, second %u): %s",
            passives.want_armor, passives.want_second, passives.status);
    else
        snprintf(status, sizeof status, "%s%s", readable ? "" : "record unreadable: ", passives.status);

    if (strcmp(status, previous))
    {
        api->log(mod, "Blunt-Force Mitigation: %s", status);
        memcpy(previous, status, strlen(status) + 1);
    }
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 6 || api->size < offsetof(HfApi, on_game_thread) + sizeof api->on_game_thread ||
        !api->set_passives || !api->get_passives || !api->on_game_thread)
    {
        api->log(mod, "Blunt-Force Mitigation requires the SDK 6 passive and game-thread APIs");
        return 1;
    }
    if (!api->set_passives(mod, HF_PASSIVE_KIT, BLUNT_FORCE_MITIGATION))
    {
        api->log(mod, "Could not request Blunt-Force Mitigation as the second passive");
        return 1;
    }
    if (!api->on_game_thread(mod, CheckPassives, NULL))
    {
        api->log(mod, "Could not monitor the passive override; releasing it");
        if (!api->set_passives(mod, HF_PASSIVE_KIT, HF_PASSIVE_KIT))
            api->log(mod, "Could not release the passive override");
        return 1;
    }
    api->log(mod, "Requested Blunt-Force Mitigation as the second passive; solo only");
    return 0;
}
