#include "shop.h"
#include <windows.h>
#include <string.h>

/* Build 25480438: the committed PlayerHistory loadout, as reviewed in native/mod-loader/loadout_scrub.c. */
enum {
    HISTORY_CELL = 0x347CE50,
    HISTORY_COUNT = 0x2D200,
    RECORD_STRIDE = 0x1690,
    LOADOUT = 0x38,
    WEAPON_STRIDE = 0x60,
    STRATAGEM_SLOTS = 0x188,
    STRATAGEM_STRIDE = 0x30,
    STRATAGEM_COUNT = 0x788,
    MAX_RECORDS = 32,
    MAX_STRATAGEMS = 32
};

static int waiting_for_spawn;

typedef enum { PRICE_WEAPON, PRICE_STRATAGEM } PriceKind;

static const struct {
    PriceKind kind;
    uint32_t id;
    const char* name;
    unsigned requisition;
} kLoadoutPrices[] = {
    {PRICE_WEAPON, HF_ITEM_R36_ERUPTOR, "R-36 Eruptor", 1000},
    {PRICE_STRATAGEM, HF_STRATAGEM_MS11_SOLO_SILO, "MS-11 Solo Silo", 1000}
};
enum { PRICE_COUNT = sizeof kLoadoutPrices / sizeof kLoadoutPrices[0] };
static unsigned char access[PRICE_COUNT];
static unsigned test_balance = 5000;
static SRWLOCK shop_lock = SRWLOCK_INIT;

int SmrBuyAccess(HfMod* mod, size_t i)
{
    if (HF_API(mod)->game_state(mod) != HF_STATE_SHIP)
        return 0;
    AcquireSRWLockExclusive(&shop_lock);
    if (i >= PRICE_COUNT || access[i] || test_balance < kLoadoutPrices[i].requisition)
    {
        ReleaseSRWLockExclusive(&shop_lock);
        return 0;
    }
    test_balance -= kLoadoutPrices[i].requisition;
    access[i] = 1;
    unsigned remaining = test_balance;
    ReleaseSRWLockExclusive(&shop_lock);
    HF_API(mod)->log(mod, "Test purchase: %s (%u test Requisition; %u remaining)",
        kLoadoutPrices[i].name, kLoadoutPrices[i].requisition, remaining);
    return 1;
}

unsigned SmrBalance(void)
{
    AcquireSRWLockShared(&shop_lock);
    unsigned balance = test_balance;
    ReleaseSRWLockShared(&shop_lock);
    return balance;
}

int SmrShopItem(size_t i, SmrItem* out)
{
    if (i >= PRICE_COUNT) return 0;
    AcquireSRWLockShared(&shop_lock);
    *out = (SmrItem){kLoadoutPrices[i].name, kLoadoutPrices[i].requisition, access[i] != 0};
    ReleaseSRWLockShared(&shop_lock);
    return 1;
}

static unsigned ConsumeSelectedAccess(HfMod* mod, const uint32_t weapons[3], const uint32_t* stratagems, uint32_t count)
{
    unsigned total = 0;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
    {
        int weapon = kLoadoutPrices[i].kind == PRICE_WEAPON;
        const uint32_t* picks = weapon ? weapons : stratagems;
        uint32_t pick_count = weapon ? 3 : count;
        for (uint32_t j = 0; j < pick_count; ++j)
        {
            if (picks[j] != kLoadoutPrices[i].id) continue;
            total += kLoadoutPrices[i].requisition;
            AcquireSRWLockExclusive(&shop_lock);
            int had_access = access[i];
            access[i] = 0;
            ReleaseSRWLockExclusive(&shop_lock);
            HF_API(mod)->log(mod, "  %s selected: %u Requisition slips; %s",
                kLoadoutPrices[i].name, kLoadoutPrices[i].requisition,
                had_access ? "test access consumed" : "no access (not enforced)");
            break;
        }
    }
    return total;
}

static int LayoutMatches(const HfApi* api, uintptr_t game)
{
    return api->code_matches(game + 0x1361F69, "48 8B 35 E0 AE 11 02") &&
        api->code_matches(game + 0x1362043,
            "39 9E 00 D2 02 00 76 36 0F 1F 44 00 00 8B C3 48 69 D0 90 16 00 00 48 03 D6 48 39 2A "
            "75 16 48 8D 4F 08 48 83 C2 38 41 B8 F0 09 00 00") &&
        api->code_matches(game + 0x1751350,
            "48 89 5C 24 08 57 48 83 EC 20 8B 01 48 8B F9 48 8B 1D 72 BA D2 01 33 D2 41 B8 00 01 00 00 89 43 40 "
            "8B 41 60 89 43 44 8B 81 C0 00 00 00 89 43 48") &&
        api->code_matches(game + 0x11E8315,
            "3B BE 88 07 00 00 72 11 41 8B 04 24 3B F8 0F 83 C6 01 00 00 48 8B 44 24 70 "
            "48 8D 0C 7F 48 C1 E1 04");
}

static uintptr_t LocalLoadout(const HfApi* api, uintptr_t game, uint64_t peer)
{
    uintptr_t history;
    uint32_t count;
    if (!peer || api->read(game + HISTORY_CELL, &history, sizeof history) || !history ||
        api->read(history + HISTORY_COUNT, &count, sizeof count) || count > MAX_RECORDS)
        return 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t record = history + (uintptr_t)i * RECORD_STRIDE;
        uint64_t record_peer;
        if (!api->read(record, &record_peer, sizeof record_peer) && record_peer == peer)
            return record + LOADOUT;
    }
    return 0;
}

static int LogLoadout(HfMod* mod, uintptr_t loadout, const char* when)
{
    const HfApi* api = HF_API(mod);
    uint32_t weapons[3], count, stratagems[MAX_STRATAGEMS];
    if (!loadout || api->read(loadout + STRATAGEM_COUNT, &count, sizeof count) || count > MAX_STRATAGEMS)
        return 0;
    for (uint32_t i = 0; i < 3; ++i)
        if (api->read(loadout + i * WEAPON_STRIDE, &weapons[i], sizeof weapons[i]) || !weapons[i])
            return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (api->read(loadout + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE, &stratagems[i], sizeof stratagems[i]))
            return 0;

    api->log(mod, "%s loadout: primary=0x%08X secondary=0x%08X throwable=0x%08X stratagem_slots=%u",
        when, weapons[0], weapons[1], weapons[2], count);
    for (uint32_t i = 0; i < count; ++i)
        api->log(mod, "  stratagem[%u] type=%u", i + 1, stratagems[i]);
    unsigned total = ConsumeSelectedAccess(mod, weapons, stratagems, count);
    api->log(mod, "  Total listed loadout price: %u Requisition slips (not charged)", total);
    return 1;
}

static void MissionStart(HfMod* mod, const HfStateEvent* event, void* user)
{
    const HfApi* api = HF_API(mod);
    uintptr_t game = api->module_base("game.dll", NULL);
    waiting_for_spawn = 0;
    if (!game || !LayoutMatches(api, game))
    {
        api->log(mod, "Mission start: loadout not read; game layout does not match the reviewed build");
        return;
    }

    const HfLocalUser* local = hf_global_LocalUser(mod);
    uint64_t peer = 0;
    if (local && api->read((uintptr_t)local + offsetof(HfLocalUser, peer), &peer, sizeof peer) == 0 &&
        LogLoadout(mod, LocalLoadout(api, game, peer), "Mission start"))
        return;

    waiting_for_spawn = 1;
    api->log(mod, "Mission start: local loadout not ready; will retry on local deployment");
}

static void PlayerSpawn(HfMod* mod, const HfPlayerEvent* event, void* user)
{
    if (!waiting_for_spawn || !event->local)
        return;
    waiting_for_spawn = 0;
    const HfApi* api = HF_API(mod);
    uintptr_t game = api->module_base("game.dll", NULL);
    if (!game || !LogLoadout(mod, LocalLoadout(api, game, event->peer), "Local deployment"))
        api->log(mod, "Local deployment: local loadout could not be read");
}

static void MissionEnd(HfMod* mod, const HfStateEvent* event, void* user)
{
    if (waiting_for_spawn)
        HF_API(mod)->log(mod, "Mission end: no local deployment to retry the loadout read");
    waiting_for_spawn = 0;
}

int SmrInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 16 || strcmp(api->build_id(), "25480438") != 0 ||
        api->size < offsetof(HfApi, global) + sizeof api->global || !api->code_matches || !api->global)
    {
        api->log(mod, "Loadout logger requires SDK 16 and game build 25480438");
        return 1;
    }
    if (hf_on_mission_start(mod, MissionStart, NULL, 0) ||
        hf_on_mission_end(mod, MissionEnd, NULL, 0) ||
        hf_on_player_spawn(mod, PlayerSpawn, NULL, 0))
        return 1;
    return 0;
}
