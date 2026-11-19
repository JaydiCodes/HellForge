#include "shop.h"
#include "requisition.h"
#include <windows.h>
#include <stdio.h>
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
static int mission_active;

typedef enum { PRICE_WEAPON, PRICE_STRATAGEM } PriceKind;

static const struct {
    PriceKind kind;
    uint32_t id;
    uint32_t mix_id;
    const char* name;
    unsigned requisition;
} kLoadoutPrices[] = {
    {PRICE_WEAPON, HF_ITEM_R36_ERUPTOR, HF_ITEM_R36_ERUPTOR, "R-36 Eruptor", 1000},
    {PRICE_STRATAGEM, HF_STRATAGEM_MS11_SOLO_SILO, 699439563u, "MS-11 Solo Silo", 1000}
};
enum { PRICE_COUNT = sizeof kLoadoutPrices / sizeof kLoadoutPrices[0] };
static unsigned char access[PRICE_COUNT];
static unsigned char deployed_access[PRICE_COUNT];
static unsigned owned_amount[PRICE_COUNT];
static unsigned wallet;
static int inventory_ready;
static int shop_failed;
static char shop_message[192] = "Waiting for the player's inventory.";
static SRWLOCK shop_lock = SRWLOCK_INIT;
static uint32_t token_weapon, token_attachment;
static const char* logs_folder;

enum { CHARGE_IDLE, CHARGE_QUEUED, CHARGE_RUNNING, CHARGE_SAVED, CHARGE_REFUSED, CHARGE_UNKNOWN };
static struct {
    int state;
    size_t item;
    char error[192];
} charge;

/* Reviewed inventory layout/functions: native/mod-loader/ownership.c, build 25480438. Only called on the game thread. */
enum {
    INVENTORY_CELL = 0x347CEF8, INVENTORY_COUNT = 0x1CE0, INVENTORY_RECORDS = 0x1CE4,
    INVENTORY_STRIDE = 0xB8, CATALOGUE_STATUS = 0xD5D48, INVENTORY_STATUS = 0xD5D4C,
    REQUISITION = 0xD7150EECu
};
static uintptr_t game_base;
static void (*inventory_add)(uintptr_t, uint32_t, uint32_t, uint32_t, const char*, int);
static void (*inventory_remove)(uintptr_t, uint32_t, uint32_t);
static void (*push_loadout)(void);

static void ShopMessage(HfMod* mod, const char* text)
{
    AcquireSRWLockExclusive(&shop_lock);
    int changed = strcmp(shop_message, text) != 0;
    snprintf(shop_message, sizeof shop_message, "%s", text);
    ReleaseSRWLockExclusive(&shop_lock);
    if (changed) HF_API(mod)->log(mod, "%s", text);
}

static uintptr_t Pointer(const HfApi* api, uintptr_t at)
{
    uintptr_t result = 0;
    return api->read(at, &result, sizeof result) ? 0 : result;
}

static uintptr_t Inventory(const HfApi* api)
{
    uintptr_t manager = Pointer(api, game_base + INVENTORY_CELL);
    uint32_t catalogue = 0, inventory = 0;
    if (!manager || api->read(manager + CATALOGUE_STATUS, &catalogue, 4) ||
        api->read(manager + INVENTORY_STATUS, &inventory, 4) || catalogue != 2 || inventory != 2)
        return 0;
    return manager;
}

static uintptr_t ItemRecord(const HfApi* api, uintptr_t manager, uint32_t mix_id, unsigned* amount)
{
    static struct { uintptr_t manager, record; uint32_t id; } cache[PRICE_COUNT + 1];
    for (size_t i = 0; i < PRICE_COUNT + 1; ++i)
        if (cache[i].manager == manager && cache[i].id == mix_id)
        {
            uint32_t id;
            if (!api->read(cache[i].record + 4, &id, 4) && id == mix_id &&
                !api->read(cache[i].record + 0xB0, amount, 4)) return cache[i].record;
            cache[i].manager = 0;
        }
    uint32_t count;
    if (api->read(manager + INVENTORY_COUNT, &count, 4) || count > 4096) return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t record = manager + INVENTORY_RECORDS + (uintptr_t)i * INVENTORY_STRIDE;
        uint32_t id;
        if (api->read(record + 4, &id, 4)) return 0;
        if (id == mix_id)
        {
            if (api->read(record + 0xB0, amount, 4)) return 0;
            for (size_t k = 0; k < PRICE_COUNT + 1; ++k)
                if (!cache[k].manager || cache[k].manager != manager)
                {
                    cache[k].manager = manager;
                    cache[k].record = record;
                    cache[k].id = mix_id;
                    break;
                }
            return record;
        }
    }
    return 0;
}

static DWORD WINAPI SpendWorker(void* unused)
{
    char error[192] = "";
    int result = SmrSpendRequisition(logs_folder, token_attachment, kLoadoutPrices[charge.item].requisition, error, sizeof error);
    AcquireSRWLockExclusive(&shop_lock);
    snprintf(charge.error, sizeof charge.error, "%s", error);
    charge.state = result > 0 ? CHARGE_SAVED : result == 0 ? CHARGE_REFUSED : CHARGE_UNKNOWN;
    ReleaseSRWLockExclusive(&shop_lock);
    return 0;
}

int SmrBuyAccess(HfMod* mod, size_t i)
{
    if (HF_API(mod)->game_state(mod) != HF_STATE_SHIP)
        return 0;
    AcquireSRWLockExclusive(&shop_lock);
    if (i >= PRICE_COUNT || access[i] || !inventory_ready || !owned_amount[i] ||
        charge.state != CHARGE_IDLE || wallet < kLoadoutPrices[i].requisition)
    {
        ReleaseSRWLockExclusive(&shop_lock);
        return 0;
    }
    charge.item = i;
    charge.state = CHARGE_QUEUED;
    ReleaseSRWLockExclusive(&shop_lock);
    ShopMessage(mod, "Saving requisition charge...");
    return 1;
}

int SmrBalance(unsigned* out)
{
    AcquireSRWLockShared(&shop_lock);
    *out = wallet;
    int ready = inventory_ready;
    ReleaseSRWLockShared(&shop_lock);
    return ready;
}

void SmrMessage(char* out, size_t size)
{
    AcquireSRWLockShared(&shop_lock);
    snprintf(out, size, "%s", shop_message);
    ReleaseSRWLockShared(&shop_lock);
}

int SmrShopItem(size_t i, SmrItem* out)
{
    if (i >= PRICE_COUNT) return 0;
    AcquireSRWLockShared(&shop_lock);
    *out = (SmrItem){kLoadoutPrices[i].name, kLoadoutPrices[i].requisition, access[i] != 0,
        owned_amount[i] != 0, charge.state != CHARGE_IDLE};
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
            deployed_access[i] = access[i];
            access[i] = 0;
            ReleaseSRWLockExclusive(&shop_lock);
            HF_API(mod)->log(mod, "  %s selected: %u Requisition slips; %s",
                kLoadoutPrices[i].name, kLoadoutPrices[i].requisition,
                had_access ? "purchased access consumed (available throughout this mission)" : "NO PURCHASED ACCESS");
            break;
        }
    }
    return total;
}

static int LayoutMatches(const HfApi* api, uintptr_t game)
{
    return api->code_matches(game + 0x1361F69, "48 8B 35 E0 AE 11 02") &&
        api->code_matches(game + 0x136EF80,
            "44 89 44 24 18 48 89 4C 24 08 53 56 57 41 54 41 55 41 56 48 81 EC D8 00 00 00 "
            "45 33 E4 45 8B E9 41 8B F8 8B F2 4C 8B F1 81 FA 50 C8 84 35 0F") &&
        api->code_matches(game + 0x136F5E0, "85 D2 0F 84 91 00 00 00 53 48 83 EC 20 44 8B 91 E0 1C 00 00") &&
        api->code_matches(game + 0x136E6C1,
            "48 8B 35 30 E8 10 02 33 DB 4D 89 73 18 44 8B F3 8B AE E0 1C 00 00 "
            "4C 8D AE E4 1C 00 00 89 AE 44 1D 0D 00") &&
        api->code_matches(game + 0x11F5010,
            "B8 18 14 00 00 E8 A6 F5 F0 00 48 2B E0 4C 8B 0D 2C 7E 28 02 33 C0 "
            "45 8B 81 00 D2 02 00 45 85 C0") &&
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

/* As loadout_scrub.c does, but only for this player's gated selections, never another player's history. */
static int RestrictLoadout(HfMod* mod, const unsigned char allowed[PRICE_COUNT])
{
    const HfApi* api = HF_API(mod);
    uintptr_t ui = Pointer(api, game_base + 0x347CE28), menu = Pointer(api, game_base + 0x347CE38);
    uint32_t presenter = 0, top = 0;
    if ((ui && api->read(ui + 0x4294, &presenter, 4)) || (menu && api->read(menu, &top, 4))) return 0;
    if (presenter == 14 || top == 11) return 1; /* the picker holds its own copies; gate ownership instead */
    uintptr_t profile = Pointer(api, game_base + 0x347CDD8);
    const HfLocalUser* local = hf_global_LocalUser(mod);
    uint64_t peer = 0;
    if (!profile || !local || api->read((uintptr_t)local + offsetof(HfLocalUser, peer), &peer, sizeof peer))
        return 1; /* login has not built a local loadout yet */
    uintptr_t loadout = LocalLoadout(api, game_base, peer);
    int changed = 0;
    if (!allowed[0])
    {
        uint32_t weapon;
        if (api->read(profile + 0x40, &weapon, 4)) return 0;
        if (weapon == HF_ITEM_R36_ERUPTOR)
        {
            uint32_t replacement = HF_ITEM_AR23_LIBERATOR;
            if (api->write(profile + 0x40, &replacement, 4)) return 0;
            push_loadout();
            changed = 1;
        }
        if (loadout)
        {
            if (api->read(loadout, &weapon, 4)) return 0;
            if (weapon == HF_ITEM_R36_ERUPTOR)
            {
                uint32_t replacement = HF_ITEM_AR23_LIBERATOR;
                if (api->write(loadout, &replacement, 4)) return 0;
                changed = 1;
            }
        }
    }
    if (!allowed[1])
    {
        uint32_t pairs[MAX_STRATAGEMS * 2];
        if (api->read(profile + 0x4C, pairs, sizeof pairs)) return 0;
        int pairs_changed = 0;
        for (size_t i = 0; i < MAX_STRATAGEMS;)
        {
            if (pairs[i * 2] != 0x4FB52679u) { ++i; continue; }
            memmove(pairs + i * 2, pairs + i * 2 + 2, (MAX_STRATAGEMS - i - 1) * 8);
            memset(pairs + (MAX_STRATAGEMS - 1) * 2, 0, 8);
            pairs_changed = changed = 1;
        }
        if (pairs_changed && api->write(profile + 0x4C, pairs, sizeof pairs)) return 0;
        if (loadout)
        {
            uint32_t count;
            unsigned char slots[MAX_STRATAGEMS * STRATAGEM_STRIDE];
            if (api->read(loadout + STRATAGEM_COUNT, &count, 4) || count > MAX_STRATAGEMS ||
                api->read(loadout + STRATAGEM_SLOTS, slots, sizeof slots)) return 0;
            uint32_t before = count;
            for (uint32_t i = 0; i < count;)
            {
                uint32_t type;
                memcpy(&type, slots + i * STRATAGEM_STRIDE, 4);
                if (type != HF_STRATAGEM_MS11_SOLO_SILO) { ++i; continue; }
                memmove(slots + i * STRATAGEM_STRIDE, slots + (i + 1) * STRATAGEM_STRIDE,
                    (count - i - 1) * STRATAGEM_STRIDE);
                memset(slots + (--count) * STRATAGEM_STRIDE, 0, STRATAGEM_STRIDE);
            }
            if (count != before)
            {
                if (api->write(loadout + STRATAGEM_SLOTS, slots, sizeof slots) ||
                    api->write(loadout + STRATAGEM_COUNT, &count, 4)) return 0;
                changed = 1;
            }
        }
    }
    if (changed) api->log(mod, "Removed unpurchased selections from the local ship loadout.");
    return 1;
}

static void ShopTick(HfMod* mod, float dt, void* user)
{
    const HfApi* api = HF_API(mod);
    if (shop_failed) return;
    if (!game_base)
    {
        uintptr_t game = api->module_base("game.dll", NULL);
        if (!game || !LayoutMatches(api, game))
        {
            if (api->game_state(mod) == HF_STATE_SHIP)
                ShopMessage(mod, "Shop unavailable: inventory/loadout code does not match the reviewed build.");
            return;
        }
        game_base = game;
        inventory_add = (void (*)(uintptr_t, uint32_t, uint32_t, uint32_t, const char*, int))(game + 0x136EF80);
        inventory_remove = (void (*)(uintptr_t, uint32_t, uint32_t))(game + 0x136F5E0);
        push_loadout = (void (*)(void))(game + 0x11F5010);
    }
    uintptr_t manager = Inventory(api);
    unsigned balance = 0, amounts[PRICE_COUNT] = {0};
    int ready = manager && ItemRecord(api, manager, REQUISITION, &balance);
    unsigned token_amount = 0;
    uintptr_t token_record = ready ? ItemRecord(api, manager, token_weapon, &token_amount) : 0;
    uint32_t token_state = 0;
    ready = ready && token_record && !api->read(token_record + 0x14, &token_state, 4);
    if (ready && (token_amount || token_state == 2 || token_state == 4))
    {
        inventory_remove(manager, token_weapon, token_amount ? token_amount : 1);
        ready = ItemRecord(api, manager, token_weapon, &token_amount) &&
            !api->read(token_record + 0x14, &token_state, 4) &&
            !token_amount && token_state != 2 && token_state != 4;
        if (!ready) ShopMessage(mod, "Could not hide the fake purchase token.");
    }
    for (size_t i = 0; i < PRICE_COUNT && ready; ++i)
        ready = ItemRecord(api, manager, kLoadoutPrices[i].mix_id, &amounts[i]) != 0;
    AcquireSRWLockExclusive(&shop_lock);
    int became_ready = ready && !inventory_ready;
    inventory_ready = ready;
    wallet = balance;
    int state = charge.state;
    size_t bought = charge.item;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if (amounts[i] > owned_amount[i]) owned_amount[i] = amounts[i];
    ReleaseSRWLockExclusive(&shop_lock);
    if (!ready) return;
    if (became_ready && state == CHARGE_IDLE)
        ShopMessage(mod, "Purchases deduct real requisition from this offline profile.");

    if (state == CHARGE_QUEUED)
    {
        if (api->game_state(mod) != HF_STATE_SHIP)
        {
            AcquireSRWLockExclusive(&shop_lock);
            charge.state = CHARGE_IDLE;
            ReleaseSRWLockExclusive(&shop_lock);
            ShopMessage(mod, "Purchase cancelled before it was sent; return to the ship.");
        }
        else
        {
            AcquireSRWLockExclusive(&shop_lock);
            charge.state = CHARGE_RUNNING;
            ReleaseSRWLockExclusive(&shop_lock);
            HANDLE thread = CreateThread(NULL, 0, SpendWorker, NULL, 0, NULL);
            if (thread) CloseHandle(thread);
            else
            {
                AcquireSRWLockExclusive(&shop_lock);
                charge.state = CHARGE_IDLE;
                ReleaseSRWLockExclusive(&shop_lock);
                ShopMessage(mod, "Purchase worker could not start; no request was sent.");
            }
        }
    }
    else if (state == CHARGE_SAVED)
    {
        unsigned price = kLoadoutPrices[bought].requisition, after;
        inventory_remove(manager, REQUISITION, price);
        if (!ItemRecord(api, manager, REQUISITION, &after) || after != (balance >= price ? balance - price : 0))
        {
            AcquireSRWLockExclusive(&shop_lock);
            inventory_ready = 0;
            ReleaseSRWLockExclusive(&shop_lock);
            shop_failed = 1;
            ShopMessage(mod, "Charge saved, but the game wallet update failed verification. Shop stopped; restart to resync.");
            return;
        }
        AcquireSRWLockExclusive(&shop_lock);
        wallet = after;
        access[bought] = 1;
        charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        api->log(mod, "Purchase saved: %s (%u Requisition; %u remaining in game)",
            kLoadoutPrices[bought].name, price, after);
        ShopMessage(mod, "Purchase saved. One-mission access ready.");
    }
    else if (state == CHARGE_REFUSED || state == CHARGE_UNKNOWN)
    {
        char error[sizeof charge.error];
        AcquireSRWLockExclusive(&shop_lock);
        snprintf(error, sizeof error, "%s", charge.error);
        if (state == CHARGE_REFUSED) charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        if (state == CHARGE_UNKNOWN)
            ShopMessage(mod, "Purchase outcome unknown. Shop disabled; check the saved profile before restarting.");
        else if (error[0]) ShopMessage(mod, error);
    }

    unsigned char allowed[PRICE_COUNT];
    AcquireSRWLockShared(&shop_lock);
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        allowed[i] = access[i] || (mission_active && deployed_access[i]);
    ReleaseSRWLockShared(&shop_lock);
    for (size_t i = 0; i < PRICE_COUNT; ++i)
    {
        unsigned amount, after;
        uint32_t item_state;
        uintptr_t record = ItemRecord(api, manager, kLoadoutPrices[i].mix_id, &amount);
        if (!record || api->read(record + 0x14, &item_state, 4)) goto ownership_failed;
        unsigned wanted = allowed[i] ? owned_amount[i] : 0;
        int is_owned = item_state == 2 || item_state == 4;
        if (amount == wanted && is_owned == (wanted != 0)) continue;
        if (!wanted) inventory_remove(manager, kLoadoutPrices[i].mix_id, amount ? amount : 1);
        else if (amount > wanted) inventory_remove(manager, kLoadoutPrices[i].mix_id, amount - wanted);
        else inventory_add(manager, kLoadoutPrices[i].mix_id, wanted - amount, 0, NULL, 0);
        record = ItemRecord(api, manager, kLoadoutPrices[i].mix_id, &after);
        if (!record || after != wanted || api->read(record + 0x14, &item_state, 4) ||
            ((item_state == 2 || item_state == 4) != (wanted != 0))) goto ownership_failed;
        api->log(mod, "%s: %s (session inventory only)", kLoadoutPrices[i].name, wanted ? "access enabled" : "access locked/hidden");
    }
    if (api->game_state(mod) == HF_STATE_SHIP && !RestrictLoadout(mod, allowed))
        ShopMessage(mod, "Could not remove an unpurchased selection from the local loadout.");
    return;

ownership_failed:
    AcquireSRWLockExclusive(&shop_lock);
    inventory_ready = 0;
    ReleaseSRWLockExclusive(&shop_lock);
    shop_failed = 1;
    ShopMessage(mod, "Item ownership update failed; shop restrictions could not be verified.");
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
    api->log(mod, "  Total listed loadout price: %u Requisition slips (charged at purchase, not again at deployment)", total);
    return 1;
}

static void MissionStart(HfMod* mod, const HfStateEvent* event, void* user)
{
    const HfApi* api = HF_API(mod);
    uintptr_t game = api->module_base("game.dll", NULL);
    waiting_for_spawn = 0;
    mission_active = 1;
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
    mission_active = 0;
    memset(deployed_access, 0, sizeof deployed_access);
}

int SmrInit(const HfApi* api, HfMod* mod)
{
    if (api->version < 16 || strcmp(api->build_id(), "25480438") != 0 ||
        api->size < offsetof(HfApi, global) + sizeof api->global || !api->code_matches || !api->global ||
        !api->on_game_thread || !api->read || !api->write || !api->module_base || !api->add_weapon || !api->logs_folder)
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
