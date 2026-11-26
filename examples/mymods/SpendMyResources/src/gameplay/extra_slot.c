#include <stdbool.h>

enum {
    EXTRA_AVAILABLE = 0x136FC20, EXTRA_COLLECT_PACKAGES = 0x1753080, EXTRA_TABLE_LEA = 0x175309C,
    EXTRA_GRANT = 0x66F190, EXTRA_GRANT_LIMIT = 0x66F28E, EXTRA_OWNER = 0x33266D0,
    EXTRA_HUD = 0x346D538, EXTRA_HUD_READY = 0x24E334,
    EXTRA_MISSION_LIMIT = 16, EXTRA_DEPENDENCY_LIMIT = 32
};

typedef struct ExtraGrantEntry {
    uint32_t type, uses;
    uint8_t override_uses, mission_grant;
    uint8_t reserved[STRATAGEM_STRIDE - 10];
} ExtraGrantEntry;
_Static_assert(sizeof(ExtraGrantEntry) == STRATAGEM_STRIDE && offsetof(ExtraGrantEntry, mission_grant) == 9,
    "Native grant entry layout");

static struct {
    bool (*available)(uintptr_t inventory, uint32_t type);
    uint32_t (*packages)(uint64_t* out, uint32_t type, uint32_t unused, uint32_t level);
    void (*grant)(uintptr_t owner, uint32_t player, const ExtraGrantEntry* entries, uint32_t count);
} extra_native;

static const struct { uint32_t type; const char* name; } extra_names[] = {
#include "smr_extra_names.inc"
};
_Static_assert(sizeof extra_names / sizeof extra_names[0] <= SMR_MAX_EXTRA_CHOICES, "Vanilla stratagem catalogue size");
static SmrExtraChoice extra_options[SMR_MAX_EXTRA_CHOICES];
static size_t extra_option_count;
static struct {
    uint64_t peer;
    uintptr_t loadout;
    float seconds, next_check;
    int finished;
} extra_deployment;

const char* SmrExtraName(uint32_t type)
{
    for (size_t i = 0; i < sizeof extra_names / sizeof extra_names[0]; ++i)
        if (extra_names[i].type == type) return extra_names[i].name;
    return NULL;
}

size_t SmrExtraChoices(SmrExtraChoice* choices, size_t capacity)
{
    AcquireSRWLockShared(&shop_lock);
    size_t count = capacity < extra_option_count ? capacity : extra_option_count;
    if (count) memcpy(choices, extra_options, count * sizeof choices[0]);
    ReleaseSRWLockShared(&shop_lock);
    return count;
}

static void InitExtraSlot(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    if (!SmrGetUpgradeTuning(SMR_UPGRADE_EXTRA_SLOT)) return;
    if (!api->load_package ||
        !api->code_matches(game_base + EXTRA_AVAILABLE, "48 89 5C 24 08 48 8B D9 85 D2 75 09") ||
        !api->code_matches(game_base + EXTRA_COLLECT_PACKAGES,
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 33 FF") ||
        !api->code_matches(game_base + EXTRA_TABLE_LEA, "4C 8D 05") ||
        !api->code_matches(game_base + EXTRA_TABLE_LEA + 7, "4C 8B F1 41 8B E9") ||
        !api->code_matches(game_base + EXTRA_GRANT, "48 89 5C 24 20 48 89 4C 24 08 55 56 57 41 54 41 55") ||
        !api->code_matches(game_base + EXTRA_GRANT_LIMIT, "83 FD 10 41 0F 43 ED"))
    {
        api->log(mod, "Extra Stratagem Slot unavailable: native grant or asset code does not match the reviewed build.");
        return;
    }
    extra_native.available = (bool (*)(uintptr_t, uint32_t))(game_base + EXTRA_AVAILABLE);
    extra_native.packages = (uint32_t (*)(uint64_t*, uint32_t, uint32_t, uint32_t))(game_base + EXTRA_COLLECT_PACKAGES);
    extra_native.grant = (void (*)(uintptr_t, uint32_t, const ExtraGrantEntry*, uint32_t))(game_base + EXTRA_GRANT);
}

static uintptr_t ExtraRow(const HfApi* api, uint32_t type, HfStratagemSettings* row)
{
    int32_t displacement;
    uintptr_t lea = game_base + EXTRA_TABLE_LEA;
    if (!type || type > SMR_MAX_EXTRA_CHOICES || api->read(lea + 3, &displacement, sizeof displacement)) return 0;
    /* Follow the same table as the native package collector, including HellForge's relocated table. */
    uintptr_t at = Pointer(api, lea + 7 + displacement + (uintptr_t)type * sizeof(uintptr_t));
    return at && !api->read(at, row, sizeof *row) && row->type == type ? at : 0;
}

static int ExtraNeedsRearm(const HfStratagemSettings* row, const uint32_t* picks, uint32_t count)
{
    return row->delivery == DELIVERY_EAGLE && !ContainsPick(picks, count, HF_STRATAGEM_EAGLE_REARM);
}

static uintptr_t EligibleExtra(const HfApi* api, uintptr_t inventory, const uint32_t* picks, uint32_t count,
    uint32_t type, HfStratagemSettings* row)
{
    if (count >= EXTRA_MISSION_LIMIT || !extra_native.grant || !inventory ||
        type == HF_STRATAGEM_RESUPPLY || type == HF_STRATAGEM_DROPOFF_HELLBOMB || type == HF_STRATAGEM_EAGLE_REARM ||
        ContainsPick(picks, count, type))
        return 0;
    uintptr_t at = ExtraRow(api, type, row);
    if (!at || row->delivery == DELIVERY_MISSION || row->delivery == DELIVERY_REARM || row->hud_type == HUD_MISSION ||
        row->hud_type > HUD_SPECIAL || !row->stable_id || !row->uses || !row->code_length || row->code_length > 9 ||
        !extra_native.available(inventory, type)) return 0;
    if (ExtraNeedsRearm(row, picks, count))
    {
        HfStratagemSettings rearm;
        if (count + 2 > EXTRA_MISSION_LIMIT || !ExtraRow(api, HF_STRATAGEM_EAGLE_REARM, &rearm) ||
            rearm.delivery != DELIVERY_REARM) return 0;
    }

    const uint32_t vehicle = HF_TRAIT_VEHICLE | HF_TRAIT_EXOSUIT | HF_TRAIT_FRV | HF_TRAIT_TANK;
    if (row->traits & vehicle)
        for (uint32_t i = 0; i < count; ++i)
            if (picks[i])
            {
                HfStratagemSettings other;
                if (!ExtraRow(api, picks[i], &other) || (other.traits & vehicle)) return 0;
            }
    return at;
}

static void RefreshExtraOptions(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    SmrExtraChoice options[sizeof extra_options / sizeof extra_options[0]];
    uint32_t picks[MAX_STRATAGEMS], count;
    size_t size = 0;
    if (extra_native.grant && ReadStratagems(api, ReadPickerLoadout(mod, game_base), picks, &count))
    {
        uintptr_t inventory = Inventory(api);
        for (size_t i = 0; i < sizeof extra_names / sizeof extra_names[0]; ++i)
        {
            HfStratagemSettings row;
            if (EligibleExtra(api, inventory, picks, count, extra_names[i].type, &row))
                options[size++] = (SmrExtraChoice){extra_names[i].type, extra_names[i].name, row.hud_type + 1};
        }
    }
    AcquireSRWLockExclusive(&shop_lock);
    memcpy(extra_options, options, size * sizeof options[0]);
    extra_option_count = size;
    ReleaseSRWLockExclusive(&shop_lock);
}

static int ExtraOptionListed(uint32_t type)
{
    for (size_t i = 0; i < extra_option_count; ++i)
        if (extra_options[i].type == type) return 1;
    return 0;
}

static int ExtraPackages(HfMod* mod, uintptr_t at, const HfStratagemSettings* row)
{
    const HfApi* api = HF_API(mod);
    uint32_t levels, references;
    if (api->read(at + 0x120, &levels, sizeof levels) || levels > EXTRA_DEPENDENCY_LIMIT ||
        api->read(at + 0x180, &references, sizeof references) || references > EXTRA_DEPENDENCY_LIMIT)
        return 0;
    uintptr_t variants = Pointer(api, at + 0x118);
    struct { uint32_t level; uint8_t padding[20]; uint64_t package; } variant[EXTRA_DEPENDENCY_LIMIT];
    uint8_t additional[EXTRA_DEPENDENCY_LIMIT][16];
    if (levels && (!variants || api->read(variants, variant, levels * sizeof variant[0]))) return 0;
    if (references)
    {
        uintptr_t list = Pointer(api, at + 0x178);
        if (!list || api->read(list, additional, references * sizeof additional[0])) return 0;
    }

    int ready = HF_PACKAGE_RESIDENT;
    const uint32_t minimum = (row->package != 0) + (row->loads_item != 0) + references;
    /* The native collector resolves the equipped item's package and additional references as well as the call-in. */
    for (uint32_t i = 0; i <= levels; ++i)
    {
        uint64_t packages[EXTRA_DEPENDENCY_LIMIT + 3] = {0};
        uint32_t count = extra_native.packages(packages, row->type, 0, i ? variant[i - 1].level : 0);
        if (count < minimum || count > sizeof packages / sizeof packages[0]) return 0;
        if (i && variant[i - 1].package)
        {
            uint32_t j = 0;
            while (j < count && packages[j] != variant[i - 1].package) ++j;
            if (j == count) return 0;
        }
        for (uint32_t j = 0; j < count; ++j)
        {
            int state = api->load_package(mod, packages[j]);
            if (state != HF_PACKAGE_LOADING && state != HF_PACKAGE_RESIDENT) return 0;
            if (state == HF_PACKAGE_LOADING) ready = state;
        }
    }
    return ready;
}

typedef struct ExtraContext {
    uintptr_t owner, loadout;
    uint32_t player, count, picks[MAX_STRATAGEMS];
} ExtraContext;

static int ReadExtraContext(HfMod* mod, ExtraContext* context)
{
    const HfApi* api = HF_API(mod);
    if (api->game_state(mod) != HF_STATE_MISSION ||
        !extra_deployment.peer || extra_deployment.peer != LocalPeer(mod)) return 0;
    uintptr_t mode = (uintptr_t)hf_global_GameMode(mod);
    uintptr_t players = (uintptr_t)hf_global_PlayerManager(mod);
    uintptr_t hud = Pointer(api, game_base + EXTRA_HUD);
    uintptr_t owner = Pointer(api, game_base + EXTRA_OWNER);
    if (!mode || !players || !hud || !owner) return 0;
    uintptr_t mission = Pointer(api, mode + offsetof(HfGameMode, mission));
    uint32_t mode_id, count, life, network, id, map_count;
    uint8_t hud_ready;
    uint64_t peer;
    if (!mission ||
        api->read(mode + offsetof(HfGameMode, mode), &mode_id, 4) || mode_id != 1 ||
        api->read(hud + EXTRA_HUD_READY, &hud_ready, 1) || hud_ready != 1 ||
        api->read(players + offsetof(HfPlayerManager, count), &count, 4) || !count || count > 4 ||
        api->read(players + 0xD8, &map_count, 4) || !map_count || map_count > 4096)
        return 0;
    int found = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t player_descriptor;
        if (api->read(players + 0x2C8 + (uintptr_t)i * 0x38, &peer, 8)) return 0;
        if (peer != extra_deployment.peer) continue;
        if (api->read(players + 0x2E0 + (uintptr_t)i * 0x38, &life, 4) || life != 3 ||
            api->read(players + 0x3A8 + (uintptr_t)i * 0x20, &network, 4) || network >= 0x7FFF ||
            api->read(players + 0xE8 + (uintptr_t)i * 8, &player_descriptor, sizeof player_descriptor) ||
            !player_descriptor || api->read(player_descriptor + 8, &id, 4) || !id)
            return 0;
        found = 1;
        break;
    }
    if (!found) return 0;
    uintptr_t map = Pointer(api, players + 0xD0);
    if (!map) return 0;
    for (uint32_t i = 0; i < map_count; ++i)
    {
        uint32_t entry[2];
        if (api->read(map + i * sizeof entry, entry, sizeof entry)) return 0;
        if (entry[0] != id) continue;
        context->owner = owner;
        context->player = id;
        context->loadout = LocalLoadout(api, game_base, peer);
        return ReadStratagems(api, context->loadout, context->picks, &context->count) &&
            context->count > 0 && context->count <= EXTRA_MISSION_LIMIT;
    }
    return 0;
}

static uint32_t AppendExtra(HfMod* mod, const ExtraContext* before, const HfStratagemSettings* row)
{
    ExtraGrantEntry entries[2];
    uint32_t added = 0;
    if (ExtraNeedsRearm(row, before->picks, before->count))
        entries[added++] = (ExtraGrantEntry){.type = HF_STRATAGEM_EAGLE_REARM, .mission_grant = 1};
    entries[added++] = (ExtraGrantEntry){.type = row->type, .mission_grant = 1};

    ExtraContext current;
    if (!ReadExtraContext(mod, &current) || current.owner != before->owner || current.player != before->player ||
        current.loadout != before->loadout || current.count != before->count || current.count + added > EXTRA_MISSION_LIMIT ||
        memcmp(current.picks, before->picks, before->count * sizeof current.picks[0]))
        return 0;
    extra_native.grant(current.owner, current.player, entries, added);
    if (!ReadExtraContext(mod, &current) || current.owner != before->owner || current.player != before->player ||
        current.loadout != before->loadout || current.count != before->count + added ||
        memcmp(current.picks, before->picks, before->count * sizeof current.picks[0]))
        return 0;
    for (uint32_t i = 0; i < added; ++i)
        if (current.picks[before->count + i] != entries[i].type) return 0;
    return added;
}
