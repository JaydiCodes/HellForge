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
    MAX_STRATAGEMS = 32,
    READY_MENU = 0x347CE38,
    READY_SCREEN = 0xB0,
    READY_CARD = 0x53A78,
    READY_SELECTION = 0x1EDF0,
    READY_PEER = 0x1EE00
};
_Static_assert(PRICE_COUNT <= SMR_MAX_RESTRICTED_ITEMS, "Built-in restricted items must fit the item table.");

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

static uint64_t LocalPeer(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    const HfLocalUser* local = hf_global_LocalUser(mod);
    uint64_t peer = 0;
    if (!local || api->read((uintptr_t)local + offsetof(HfLocalUser, peer), &peer, sizeof peer)) return 0;
    return peer;
}

static uintptr_t ReadLocalLoadout(HfMod* mod)
{
    return LocalLoadout(HF_API(mod), game_base, LocalPeer(mod));
}

static uintptr_t BriefingMenu(const HfApi* api, uintptr_t game)
{
    uintptr_t menu = game ? Pointer(api, game + READY_MENU) : 0;
    uint32_t top = 0;
    return menu && !api->read(menu, &top, sizeof top) && top == 11 ? menu : 0;
}

int SmrLoadoutOpen(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    return api->game_state(mod) == HF_STATE_SHIP && BriefingMenu(api, api->module_base("game.dll", NULL));
}

/* The briefing card borrows its own Loadout (same layout as history), not the last deployed loadout.
 * 0x14704FE passes the picker copy to 0x189CA20, whose stores at 0x189CA4E bind the pointer and peer. */
static uintptr_t ReadCardLoadout(HfMod* mod, uintptr_t card)
{
    const HfApi* api = HF_API(mod);
    uint64_t peer = LocalPeer(mod), card_peer;
    if (!card || !peer || api->read(card + READY_PEER, &card_peer, sizeof card_peer) || card_peer != peer)
        return 0;
    return Pointer(api, card + READY_SELECTION);
}

static uintptr_t ReadPickerLoadout(HfMod* mod, uintptr_t game)
{
    const HfApi* api = HF_API(mod);
    uintptr_t menu = api->game_state(mod) == HF_STATE_SHIP ? BriefingMenu(api, game) : 0;
    uintptr_t screen = menu ? Pointer(api, menu + READY_SCREEN) : 0;
    return screen ? ReadCardLoadout(mod, screen + READY_CARD) : 0;
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

static int ContainsPick(const uint32_t* picks, uint32_t count, uint32_t type)
{
    for (uint32_t i = 0; i < count; ++i)
        if (picks[i] == type) return 1;
    return 0;
}

static int ReadRestrictedItems(const HfApi* api, uintptr_t loadout, SmrItemSet* selected, uint32_t* primary)
{
    uint32_t count = 0, weapons[3], stratagems[MAX_STRATAGEMS];
    if (!ReadStratagems(api, loadout, stratagems, &count)) return 0;
    for (size_t i = 0; i < 3; ++i)
        if (api->read(loadout + i * WEAPON_STRIDE, &weapons[i], sizeof weapons[i])) return 0;
    memset(selected, 0, sizeof *selected);
    if (primary) *primary = weapons[0];
    for (size_t i = 0; i < loadout_price_count; ++i)
    {
        if (!kLoadoutPrices[i].requisition) continue;
        int weapon = kLoadoutPrices[i].kind == PRICE_WEAPON;
        uint32_t type = kLoadoutPrices[i].id;
        if (weapon ? ContainsPick(weapons, 3, type) : ContainsPick(stratagems, count, type))
            SmrItemSetAdd(selected, i);
    }
    return 1;
}

static int ReadEquippedItems(HfMod* mod, SmrItemSet* selected, uint32_t* primary)
{
    return ReadRestrictedItems(HF_API(mod), ReadPickerLoadout(mod, game_base), selected, primary);
}

/* Called under shop_lock: a checkout pick belongs to the item it was made for, not its slot. */
static void UpdateCheckoutLoadout(SmrItemSet equipped, uint32_t primary, int valid)
{
    checkout_equipped = equipped;
    checkout_valid = valid;
    if (checkout_primary != primary || !valid) memset(equipment_selected, 0, sizeof equipment_selected);
    checkout_primary = valid ? primary : 0;
}

/* Merge the extra under shop_lock so a new or removed upgrade changes the quote immediately. */
static SmrItemSet WithExtraAccess(SmrItemSet equipped)
{
    if (extra_pick &&
        (upgrade_selected == SMR_UPGRADE_EXTRA_SLOT || upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT))
        for (size_t i = 0; i < loadout_price_count; ++i)
            if (kLoadoutPrices[i].requisition && kLoadoutPrices[i].kind == PRICE_STRATAGEM &&
                kLoadoutPrices[i].id == extra_pick)
            {
                SmrItemSetAdd(&equipped, i);
                return equipped;
            }
    return equipped;
}

static SmrItemSet UnpaidItems(const SmrItemSet* selected, unsigned* total)
{
    SmrItemSet unpaid = {0};
    *total = 0;
    for (size_t i = 0; i < loadout_price_count; ++i)
        if (SmrItemSetHas(selected, i) && !access[i])
        {
            SmrItemSetAdd(&unpaid, i);
            *total += kLoadoutPrices[i].requisition;
        }
    return unpaid;
}
