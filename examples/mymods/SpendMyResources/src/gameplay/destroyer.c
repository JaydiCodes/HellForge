#include <float.h>

static const HfField kCooldown = HF_FIELD(StratagemSettings, cooldown);
static const HfField kDelivery = HF_FIELD(StratagemSettings, delivery);
static const HfField kHudType = HF_FIELD(StratagemSettings, hud_type);
static const HfField kUses = HF_FIELD(StratagemSettings, uses);
static const HfField kCallIn = HF_FIELD(StratagemSettings, call_in_time);
enum { MAX_UPGRADE_EDITS = MAX_STRATAGEMS * 2 + 1 };
typedef struct UpgradeEdit {
    uint32_t type;
    const HfField* field;
    HfValue value;
} UpgradeEdit;
static UpgradeEdit applied_edits[MAX_UPGRADE_EDITS];
static size_t upgraded_count;
static SmrUpgradeTuning upgrade_tuning[SMR_UPGRADE_COUNT];

const SmrUpgradeTuning* SmrGetUpgradeTuning(SmrUpgrade upgrade)
{
    if (upgrade <= SMR_UPGRADE_NONE || upgrade >= SMR_UPGRADE_COUNT ||
        !upgrade_tuning[upgrade].price[FORGE_CREDITS]) return NULL;
    return &upgrade_tuning[upgrade];
}

static int UpgradePrice(SmrUpgrade upgrade, unsigned price[CURRENCY_COUNT])
{
    const SmrUpgradeTuning* tuning = SmrGetUpgradeTuning(upgrade);
    if (tuning) memcpy(price, tuning->price, sizeof tuning->price);
    else memset(price, 0, sizeof tuning->price);
    return tuning != NULL;
}

int SmrTakeDestroyerRequest(void)
{
    return InterlockedCompareExchange(&destroyer_gate, DESTROYER_OPEN, DESTROYER_REQUESTED) == DESTROYER_REQUESTED;
}

void SmrFinishDestroyer(int proceed)
{
    if (proceed) InterlockedCompareExchange(&destroyer_gate, DESTROYER_CONTINUE, DESTROYER_OPEN);
    else InterlockedExchange(&destroyer_gate, DESTROYER_IDLE);
}

static int DestroyerSolo(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    if (!api->session || !api->get_live || !api->set_live) return 0;
    HfSession session = {0};
    api->session(mod, &session);
    return session.role == HF_SESSION_SOLO;
}

SmrDestroyerStatus SmrDestroyer(HfMod* mod)
{
    int solo = DestroyerSolo(mod);
    AcquireSRWLockShared(&shop_lock);
    SmrDestroyerStatus status = {{0},
        solo && upgrade_funds_ready && !shop_failed && !upgrade_purchased && !upgrade_selected,
        solo, upgrade_selected, upgrade_purchased, extra_pick};
    memcpy(status.balance, currency_amount, sizeof status.balance);
    ReleaseSRWLockShared(&shop_lock);
    return status;
}

int SmrSelectUpgrade(HfMod* mod, SmrUpgrade upgrade, uint32_t extra)
{
    int eligible = HF_API(mod)->game_state(mod) == HF_STATE_SHIP && DestroyerSolo(mod);
    AcquireSRWLockExclusive(&shop_lock);
    int allowed = eligible && SmrGetUpgradeTuning(upgrade) &&
        InterlockedCompareExchange(&destroyer_gate, 0, 0) == DESTROYER_OPEN &&
        upgrade_funds_ready && !shop_failed && !upgrade_purchased && !upgrade_selected &&
        (upgrade == SMR_UPGRADE_EXTRA_SLOT ? ExtraOptionListed(extra) : extra == 0);
    if (allowed)
    {
        upgrade_selected = upgrade;
        extra_pick = extra;
    }
    ReleaseSRWLockExclusive(&shop_lock);
    if (!allowed) ShopMessage(mod, "Destroyer upgrade selection unavailable.");
    return allowed;
}

static int AddTimedEdit(HfMod* mod, UpgradeEdit* edits, size_t* count, uint32_t type,
    const HfField* field, double factor, double seconds)
{
    HfLiveValue current;
    if (!HF_API(mod)->get_live(mod, type, field, &current)) return 0;
    double value = current.now.number * factor + seconds;
    if (!(value >= 0 && value <= FLT_MAX)) return 0;
    edits[(*count)++] = (UpgradeEdit){type, field, HF_F32(value)};
    return 1;
}

static int AddFixedEdit(HfMod* mod, UpgradeEdit* edits, size_t* count, uint32_t type,
    const HfField* field, HfValue value)
{
    HfLiveValue current;
    if (!HF_API(mod)->get_live(mod, type, field, &current)) return 0;
    edits[(*count)++] = (UpgradeEdit){type, field, value};
    return 1;
}

static int PlanUpgrade(HfMod* mod, uintptr_t loadout, SmrUpgrade upgrade, uint32_t extra, UpgradeEdit* edits, size_t* count)
{
    const HfApi* api = HF_API(mod);
    uint32_t picks[MAX_STRATAGEMS], slots;
    *count = 0;
    const SmrUpgradeTuning* tuning = SmrGetUpgradeTuning(upgrade);
    if (!tuning || !ReadStratagems(api, loadout, picks, &slots)) return 0;
    uint32_t native_slots = slots;
    if (extra)
    {
        if (slots >= MAX_STRATAGEMS || ContainsPick(picks, slots, extra)) return 0;
        picks[slots++] = extra;
    }
    int deployed_extra = extra && api->game_state(mod) == HF_STATE_MISSION;
    const double cooldown_factor = (100 + tuning->cooldown_percent) / 100;
    const double other_factor = 1 + tuning->other_cooldown_percent / 100;
    int has_eagle = 0;
    for (uint32_t i = 0; i < slots; ++i)
    {
        uint32_t type = picks[i];
        if (!type || type == HF_STRATAGEM_RESUPPLY || type == HF_STRATAGEM_DROPOFF_HELLBOMB ||
            type == HF_STRATAGEM_EAGLE_REARM) continue;
        if (deployed_extra && i < native_slots)
        {
            uint8_t mission_grant;
            if (api->read(loadout + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE + offsetof(ExtraGrantEntry, mission_grant),
                &mission_grant, sizeof mission_grant)) return 0;
            if (mission_grant) continue;
        }
        uint32_t j = 0;
        while (j < i && picks[j] != type) ++j;
        if (j < i) continue;
        HfLiveValue delivery, hud;
        if (!api->get_live(mod, type, &kDelivery, &delivery) ||
            !api->get_live(mod, type, &kHudType, &hud)) return 0;
        if (hud.now.number == HUD_MISSION) continue;
        int eagle = delivery.now.number == DELIVERY_EAGLE;
        int orbital = delivery.now.number == DELIVERY_ORBITAL || delivery.now.number == DELIVERY_BARRAGE;
        has_eagle |= eagle;
        if (upgrade == SMR_UPGRADE_EAGLE && eagle)
        {
            if (!AddFixedEdit(mod, edits, count, type, &kCooldown, (HfValue)HF_F32(tuning->cooldown_seconds)) ||
                !AddTimedEdit(mod, edits, count, type, &kCallIn, 1, tuning->call_in_extra_seconds)) return 0;
        }
        else if ((upgrade == SMR_UPGRADE_DEFENSIVE && hud.now.number == HUD_DEFENSIVE) ||
            (upgrade == SMR_UPGRADE_SUPPORT && hud.now.number == HUD_SUPPORT))
        {
            /* StratagemSettings stores unlimited uses as UINT32_MAX. */
            if (!AddTimedEdit(mod, edits, count, type, &kCooldown, cooldown_factor, 0) ||
                !AddFixedEdit(mod, edits, count, type, &kUses, (HfValue)HF_U32(UINT32_MAX))) return 0;
        }
        else if (!AddTimedEdit(mod, edits, count, type, &kCooldown,
            upgrade == SMR_UPGRADE_ORBITAL && orbital ? cooldown_factor : other_factor, 0))
            return 0;
    }
    if (has_eagle)
    {
        /* All vanilla Eagles share this rearm row. */
        if (upgrade == SMR_UPGRADE_EAGLE)
        {
            if (!AddFixedEdit(mod, edits, count, HF_STRATAGEM_EAGLE_REARM, &kCooldown,
                (HfValue)HF_F32(tuning->rearm_seconds)))
                return 0;
        }
        else if (!AddTimedEdit(mod, edits, count, HF_STRATAGEM_EAGLE_REARM, &kCooldown, other_factor, 0)) return 0;
    }
    return *count != 0;
}

static int CanApplyUpgrade(HfMod* mod, SmrUpgrade upgrade, uint32_t extra)
{
    const HfApi* api = HF_API(mod);
    UpgradeEdit edits[MAX_UPGRADE_EDITS];
    size_t count;
    uintptr_t loadout = ReadLocalLoadout(mod);
    if (!DestroyerSolo(mod) || !PlanUpgrade(mod, loadout, upgrade, extra, edits, &count)) return 0;
    if (upgrade == SMR_UPGRADE_EXTRA_SLOT)
    {
        HfStratagemSettings row;
        uint32_t picks[MAX_STRATAGEMS], slots;
        if (!ReadStratagems(api, loadout, picks, &slots)) return 0;
        uintptr_t at = EligibleExtra(api, Inventory(api), picks, slots, extra, &row);
        if (!at || !ExtraPackages(mod, at, &row))
        {
            api->log(mod, "Extra Stratagem Slot checkout refused: pick is no longer eligible or its assets are unavailable.");
            return 0;
        }
    }
    return 1;
}

static void ResetUpgrade(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    while (upgraded_count)
    {
        const UpgradeEdit* edit = &applied_edits[--upgraded_count];
        api->set_live(mod, edit->type, edit->field, NULL, 0);
    }
}

static int ApplyUpgradeEdits(HfMod* mod, const UpgradeEdit* edits, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (!HF_API(mod)->set_live(mod, edits[i].type, edits[i].field, &edits[i].value, 0)) return 0;
        applied_edits[upgraded_count++] = edits[i];
    }
    return 1;
}

static void ApplyUpgrade(HfMod* mod, uintptr_t loadout, int preparing)
{
    AcquireSRWLockShared(&shop_lock);
    SmrUpgrade purchased = upgrade_purchased;
    ReleaseSRWLockShared(&shop_lock);
    if (!purchased || purchased == SMR_UPGRADE_EXTRA_SLOT || upgraded_count || !DestroyerSolo(mod)) return;
    const HfApi* api = HF_API(mod);
    UpgradeEdit edits[MAX_UPGRADE_EDITS];
    size_t count;
    if (PlanUpgrade(mod, loadout, purchased, 0, edits, &count))
    {
        if (!preparing)
            for (size_t i = 0; i < count; ++i)
                if (edits[i].field == &kUses)
                {
                    ShopMessage(mod, "Too late to set stratagem uses; upgrade retained for a later solo mission.");
                    return;
                }
        if (ApplyUpgradeEdits(mod, edits, count))
        {
            api->log(mod, "Destroyer upgrade %d active: %u stratagem fields updated.", purchased, (unsigned)count);
            return;
        }
    }
    ResetUpgrade(mod);
    ShopMessage(mod, "Destroyer upgrade could not be applied; purchase retained for a later solo mission.");
}
