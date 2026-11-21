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
_Static_assert(PRICE_COUNT <= SMR_MAX_RESTRICTED_ITEMS, "Restricted items must fit the quote and checkout mask.");

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

static uintptr_t ReadLocalLoadout(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    const HfLocalUser* local = hf_global_LocalUser(mod);
    uint64_t peer = 0;
    if (!local || api->read((uintptr_t)local + offsetof(HfLocalUser, peer), &peer, sizeof peer)) return 0;
    return LocalLoadout(api, game_base, peer);
}

static int ReadStratagems(const HfApi* api, uintptr_t loadout, uint32_t* stratagems, uint32_t* count)
{
    if (!loadout || api->read(loadout + STRATAGEM_COUNT, count, sizeof *count) || *count > MAX_STRATAGEMS)
        return 0;
    for (uint32_t i = 0; i < *count; ++i)
        if (api->read(loadout + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE, &stratagems[i], sizeof stratagems[i]))
            return 0;
    return 1;
}

static int SelectedItems(HfMod* mod, uint64_t* selected)
{
    const HfApi* api = HF_API(mod);
    uintptr_t loadout = ReadLocalLoadout(mod);
    uint32_t count = 0, weapons[3], stratagems[MAX_STRATAGEMS];
    if (!ReadStratagems(api, loadout, stratagems, &count)) return 0;
    for (size_t i = 0; i < 3; ++i)
        if (api->read(loadout + i * WEAPON_STRIDE, &weapons[i], sizeof weapons[i])) return 0;
    *selected = 0;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
    {
        int weapon = kLoadoutPrices[i].kind == PRICE_WEAPON;
        const uint32_t* picks = weapon ? weapons : stratagems;
        for (uint32_t j = 0; j < (weapon ? 3u : count); ++j)
            if (picks[j] == kLoadoutPrices[i].id) *selected |= UINT64_C(1) << i;
    }
    return 1;
}

static uint64_t UnpaidItems(uint64_t selected, unsigned* total)
{
    uint64_t unpaid = 0;
    *total = 0;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if ((selected & (UINT64_C(1) << i)) && !access[i])
        {
            unpaid |= UINT64_C(1) << i;
            *total += kLoadoutPrices[i].requisition;
        }
    return unpaid;
}
