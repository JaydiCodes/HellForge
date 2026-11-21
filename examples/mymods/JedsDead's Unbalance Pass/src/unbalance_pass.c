#include "unbalance_pass.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define BLUNT_FORCE_MITIGATION 40u
#define KNOCKDOWN_ROW (BLUNT_FORCE_MITIGATION * 16u)

static atomic_int requested = UNBALANCE_DEFAULT_MITIGATION;
static atomic_int mitigationState = UNBALANCE_WAITING;
static int applied = UNBALANCE_DEFAULT_MITIGATION;

void UnbalanceRequestMitigation(int percent)
{
    atomic_store(&requested, percent < 0 ? 0 : percent > 100 ? 100 : percent);
}

UnbalanceMitigationState UnbalanceMitigationStatus(void)
{
    return (UnbalanceMitigationState)atomic_load(&mitigationState);
}

static void UpdateMitigation(HfMod* mod, const HfPassives* passives, int readable)
{
    const HfApi* api = HF_API(mod);
    int solo = readable && passives->players == 1;
    int wanted = solo ? atomic_load(&requested) : UNBALANCE_DEFAULT_MITIGATION;
    UnbalanceMitigationState state = solo ? UNBALANCE_SOLO :
        readable && passives->players > 1 ? UNBALANCE_COOP : UNBALANCE_WAITING;
    if (wanted != applied)
    {
        const HfField field = HF_FIELD(PassiveModifier, value);
        const HfValue value = HF_F32((100 - wanted) / 100.0);
        /* Release only this override, leaving the stats file and other mods in place. */
        const HfValue* edit = wanted == UNBALANCE_DEFAULT_MITIGATION ? NULL : &value;
        if (edit && !api->get_live(mod, KNOCKDOWN_ROW, &field, &(HfLiveValue){0}))
            state = UNBALANCE_WAITING;
        else if (api->set_live(mod, KNOCKDOWN_ROW, &field, edit, HF_LIVE_SHARED))
            applied = wanted;
        else
            state = solo ? UNBALANCE_REFUSED : UNBALANCE_RESTORE_REFUSED;
    }
    atomic_store(&mitigationState, state);
}

static void CheckPassives(HfMod* mod, float dt, void* user)
{
    static float elapsed = 0.25f;
    static char previous[256];
    char status[256];
    HfPassives passives;
    const HfApi* api = HF_API(mod);

    elapsed += dt;
    if (elapsed < 0.25f) return;
    elapsed = 0.0f;

    int readable = api->get_passives(mod, &passives);
    UpdateMitigation(mod, &passives, readable);
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

int UnbalanceInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 6 || api->size < offsetof(HfApi, on_game_thread) + sizeof api->on_game_thread ||
        !api->set_passives || !api->get_passives || !api->set_live || !api->get_live || !api->on_game_thread)
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

#ifndef UNBALANCE_OVERLAY
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    return UnbalanceInit(api, mod);
}
#endif
