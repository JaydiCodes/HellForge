#include "unbalance_pass.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define BLUNT_FORCE_MITIGATION 40u
#define KNOCKDOWN_ROW (BLUNT_FORCE_MITIGATION * 16u)

static const struct { HfRef entity; uint32_t projectile; } primarySwaps[] = {
    {HF_ENTITY_AR11_ARBITRATOR, 276},
    {HF_ENTITY_AR61_TENDERIZER, 135},
    {HF_ENTITY_ARGL21_ONETWO, 135},
    {HF_ENTITY_M7S_SMG, 215},
    {HF_ENTITY_MA5C_ASSAULT_RIFLE, 135},
    {HF_ENTITY_R2_AMENDMENT, 305},
    {HF_ENTITY_SMG203_GALLANT, 276},
    {HF_ENTITY_GATLING_SENTRY, 275},
    {HF_ENTITY_AMG43_MACHINE_GUN_SENTRY, 135},
    {0xA32621E3BDE13379ull, 276}, /* AX/AR-23 Guard Dog / gun */
    {0x08F6089289C83D22ull, 275}, /* EXO-45 Patriot Exosuit / right_gun */
    {0xE5F64DCC3BFE9DD1ull, 115}, /* EXO-49 Emancipator Exosuit / left_gun */
    {0xFF9878576A4C543Bull, 115}, /* EXO-49 Emancipator Exosuit / right_gun */
    {0x4DF41F84668D07FBull, 276}, /* M-103 Supply FRV / gun */
    {HF_ENTITY_SMG32_REPRIMAND, 135},
    {HF_ENTITY_SMGFLAM34_STOKER, 215},
    {HF_ENTITY_GL21_GRENADE_LAUNCHER, 89},
    {HF_ENTITY_RL77_AIRBURST_ROCKET_LAUNCHER, 36},
};

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

static void QueueProjectileSwaps(const HfApi* api, HfMod* mod)
{
    /* Deferred SDK edits resolve these components after entity readiness, unlike the JSON stats package retry. */
    const size_t primaryCount = sizeof primarySwaps / sizeof primarySwaps[0];
    size_t refused = 0;
    for (size_t i = 0; i < primaryCount; i++)
        if (hf_set(mod, primarySwaps[i].entity, ProjectileWeapon, projectile, HF_U32(primarySwaps[i].projectile))) refused++;
    if (hf_set(mod, HF_ENTITY_R2124_CONSTITUTION, WeaponRounds, primary_projectile, HF_U32(305))) refused++;
    api->log(mod, "Projectile swaps: %zu queued, %zu refused", primaryCount + 1 - refused, refused);
}

int UnbalanceInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 6 || api->size < offsetof(HfApi, on_game_thread) + sizeof api->on_game_thread ||
        !api->set_passives || !api->get_passives || !api->set_live || !api->get_live || !api->on_game_thread || !api->set_field)
    {
        api->log(mod, "UnBalance Pass requires the SDK 6 passive, game-thread and deferred field APIs");
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
    QueueProjectileSwaps(api, mod);
    return 0;
}

#ifndef UNBALANCE_OVERLAY
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    return UnbalanceInit(api, mod);
}
#endif
