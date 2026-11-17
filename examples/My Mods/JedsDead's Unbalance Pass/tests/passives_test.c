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

int main(void)
{
    HfApi api = {0};
    api.version = 6;
    api.size = sizeof api;
    api.log = Log;
    api.set_passives = SetPassives;
    api.get_passives = GetPassives;
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

    tick_result = 1;
    assert(hellforge_mod_init(&api, &mod) == 0);
    assert(last_armor == HF_PASSIVE_KIT && last_second == BLUNT_FORCE_MITIGATION && registered_tick);

    state.want_armor = HF_PASSIVE_KIT;
    state.want_second = BLUNT_FORCE_MITIGATION;
    strcpy(state.status, "held");
    registered_tick(&mod, 0.0f, NULL);
    assert(strstr(last_log, "held"));
    unsigned logs = log_calls;
    registered_tick(&mod, 2.0f, NULL);
    assert(log_calls == logs);

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
    puts("passive override tests passed");
    return 0;
}
