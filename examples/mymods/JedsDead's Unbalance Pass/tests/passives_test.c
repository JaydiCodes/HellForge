#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../src/unbalance_pass.c"

struct HfMod { const HfApi* api; };

static int set_result = 1, tick_result = 1, readable = 1;
static unsigned set_calls, log_calls;
static uint32_t last_armor, last_second;
static char last_log[512];
static HfTickCallback registered_tick;
static HfPassives state;
static int row_ready = 1, live_allowed = 1;
static unsigned live_calls, releases;
static double multiplier = 0.4;
static HfRef swap_targets[19];
static unsigned swap_calls, swap_releases, feed_calls;
static int refuse_arbitrator;

static void Log(HfMod* mod, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_log, sizeof last_log, format, args);
    va_end(args);
    log_calls++;
}

static int SetPassives(HfMod* mod, uint32_t armor, uint32_t second)
{
    set_calls++;
    last_armor = armor;
    last_second = second;
    return set_result;
}

static int GetPassives(HfMod* mod, HfPassives* out)
{
    *out = state;
    return readable;
}

static int OnGameThread(HfMod* mod, HfTickCallback callback, void* user)
{
    registered_tick = callback;
    return tick_result;
}

static int GetLive(HfMod* mod, HfRef target, const HfField* field, HfLiveValue* out)
{
    assert(target == 640 && !strcmp(field->component, "PassiveModifier") && field->offset == 8);
    if (row_ready && out) out->now = (HfValue)HF_F32(multiplier);
    return row_ready;
}

/* A projectile swap's live edit: a set carries the swapped type, a NULL value is its release. */
static int SwapLive(HfRef target, const HfField* field, const HfValue* value)
{
    assert(target && swap_calls < sizeof swap_targets / sizeof swap_targets[0]);
    for (unsigned i = 0; i < swap_calls; i++) assert(swap_targets[i] != target);
    swap_targets[swap_calls++] = target;
    assert(field->size == 4 && field->type == HF_TYPE_U32 && (!value || value->type == HF_TYPE_U32));
    if (!value) swap_releases++;
    if (target == HF_ENTITY_R2124_CONSTITUTION)
    {
        assert(!strcmp(field->component, "WeaponRoundsComponentData") && field->offset == 64 && (!value || value->number == 305));
        feed_calls++;
    }
    else
    {
        assert(!strcmp(field->component, "ProjectileWeaponComponentData") && field->offset == 0);
        if (value && target == HF_ENTITY_AR61_TENDERIZER) assert(value->number == 135);
        if (target == HF_ENTITY_AR11_ARBITRATOR)
        {
            assert(!value || value->number == 276);
            if (value && refuse_arbitrator) return 0;
        }
    }
    return HF_APPLIES_NOW;
}

static int SetLive(HfMod* mod, HfRef target, const HfField* field, const HfValue* value, uint32_t flags)
{
    if (strcmp(field->component, "PassiveModifier") != 0)
    {
        assert(flags == 0);
        return SwapLive(target, field, value);
    }
    assert(GetLive(mod, target, field, NULL) || !value);
    assert(flags == HF_LIVE_SHARED);
    ++live_calls;
    if (!live_allowed) return 0;
    if (value) assert(value->type == HF_TYPE_F32);
    else ++releases;
    multiplier = value ? value->number : 0.4;
    return HF_APPLIES_NOW;
}

int main(void)
{
    HfApi api = {0};
    api.version = 6;
    api.size = sizeof api;
    api.log = Log;
    api.set_passives = SetPassives;
    api.get_passives = GetPassives;
    api.get_live = GetLive;
    api.set_live = SetLive;
    api.on_game_thread = OnGameThread;
    HfMod mod = {&api};

    api.version = 5;
    assert(hellforge_mod_init(&api, &mod) != 0 && set_calls == 0);
    api.version = 6;
    api.size = offsetof(HfApi, on_game_thread);
    assert(hellforge_mod_init(&api, &mod) != 0 && set_calls == 0);
    api.size = sizeof api;

    set_result = 0;
    assert(hellforge_mod_init(&api, &mod) != 0 && set_calls == 1 && registered_tick == NULL);
    set_result = 1;
    tick_result = 0;
    assert(hellforge_mod_init(&api, &mod) != 0 && set_calls == 3);
    assert(last_armor == HF_PASSIVE_KIT && last_second == HF_PASSIVE_KIT);
    assert(swap_calls == 0);

    tick_result = 1;
    assert(hellforge_mod_init(&api, &mod) == 0);
    assert(last_armor == HF_PASSIVE_KIT && last_second == BLUNT_FORCE_MITIGATION && registered_tick);
    assert(swap_calls == 0); /* the swaps are live edits: nothing is written before the first tick */

    state.want_armor = HF_PASSIVE_KIT;
    state.want_second = BLUNT_FORCE_MITIGATION;
    strcpy(state.status, "held");
    registered_tick(&mod, 0.0f, NULL);
    assert(swap_calls == 19 && feed_calls == 1 && swap_releases == 0);
    assert(strstr(last_log, "held"));
    unsigned logs = log_calls;
    registered_tick(&mod, 2.0f, NULL);
    assert(log_calls == logs && swap_calls == 19);

    /* The F7 menu's switch: off releases every swap, on puts them back, and a refusal is counted. */
    swap_calls = feed_calls = 0;
    UnbalanceSetProjectileSwaps(0);
    registered_tick(&mod, 2.0f, NULL);
    assert(swap_calls == 19 && swap_releases == 19 && feed_calls == 1 && strstr(last_log, "off: 19 released, 0 refused"));
    swap_calls = feed_calls = swap_releases = 0;
    refuse_arbitrator = 1;
    UnbalanceSetProjectileSwaps(1);
    registered_tick(&mod, 2.0f, NULL);
    assert(swap_calls == 19 && feed_calls == 1 && strstr(last_log, "on: 18 applied, 1 refused"));
    swap_calls = feed_calls = 0;
    refuse_arbitrator = 0;
    UnbalanceSetProjectileSwaps(0);
    registered_tick(&mod, 2.0f, NULL);
    assert(swap_calls == 19 && swap_releases == 19 && strstr(last_log, "off: 19 released, 0 refused"));
    swap_calls = feed_calls = swap_releases = 0;
    UnbalanceSetProjectileSwaps(1);
    registered_tick(&mod, 2.0f, NULL);
    assert(swap_calls == 19 && feed_calls == 1 && strstr(last_log, "on: 19 applied, 0 refused"));
    swap_calls = 0;
    registered_tick(&mod, 2.0f, NULL);
    assert(swap_calls == 0); /* the wanted state holds: nothing is written again */

    strcpy(state.status, "solo only");
    registered_tick(&mod, 2.0f, NULL);
    assert(strstr(last_log, "solo only"));
    state.want_second = 0;
    registered_tick(&mod, 2.0f, NULL);
    assert(strstr(last_log, "override changed by another caller"));
    state.want_second = BLUNT_FORCE_MITIGATION;
    readable = 0;
    strcpy(state.status, "waiting for the game");
    registered_tick(&mod, 2.0f, NULL);
    assert(strstr(last_log, "record unreadable") && strstr(last_log, "waiting for the game"));

    assert(live_calls == 0 && multiplier == 0.4);
    UnbalanceRequestMitigation(95);
    state.players = 1;
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 0 && UnbalanceMitigationStatus() == UNBALANCE_WAITING);
    readable = 1;
    row_ready = 0;
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 0 && UnbalanceMitigationStatus() == UNBALANCE_WAITING);
    row_ready = 1;
    live_allowed = 0;
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 1 && multiplier == 0.4 && UnbalanceMitigationStatus() == UNBALANCE_REFUSED);
    live_allowed = 1;
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 2 && multiplier == 0.05 && UnbalanceMitigationStatus() == UNBALANCE_SOLO);
    UnbalanceRequestMitigation(100);
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 3 && multiplier == 0.0);
    state.players = 2;
    live_allowed = 0;
    registered_tick(&mod, 0.25f, NULL);
    assert(releases == 0 && UnbalanceMitigationStatus() == UNBALANCE_RESTORE_REFUSED);
    live_allowed = 1;
    registered_tick(&mod, 0.25f, NULL);
    assert(releases == 1 && multiplier == 0.4 && UnbalanceMitigationStatus() == UNBALANCE_COOP);
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 5);
    state.players = 1;
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == 6 && multiplier == 0.0);
    UnbalanceRequestMitigation(0);
    registered_tick(&mod, 0.25f, NULL);
    assert(multiplier == 1.0);
    UnbalanceRequestMitigation(70);
    registered_tick(&mod, 0.25f, NULL);
    assert(releases == 1 && multiplier == 0.3);
    UnbalanceRequestMitigation(UNBALANCE_DEFAULT_MITIGATION);
    registered_tick(&mod, 0.25f, NULL);
    assert(releases == 1 && multiplier == 0.4);
    multiplier = 0.7;
    registered_tick(&mod, 0.25f, NULL);
    assert(multiplier == 0.4 && UnbalanceMitigationStatus() == UNBALANCE_SOLO);
    unsigned calls = live_calls;
    registered_tick(&mod, 0.25f, NULL);
    assert(live_calls == calls);
    UnbalanceRequestMitigation(50);
    registered_tick(&mod, 0.25f, NULL);
    readable = 0;
    registered_tick(&mod, 0.25f, NULL);
    assert(releases == 2 && multiplier == 0.4 && UnbalanceMitigationStatus() == UNBALANCE_WAITING);
    puts("passive override and live projectile swap tests passed");
    return 0;
}
