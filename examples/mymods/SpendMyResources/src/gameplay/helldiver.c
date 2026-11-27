#include <math.h>

/* The Helldiver's avatar entity (its shared Avatar record). */
#define HELLDIVER_RESOURCE UINT64_C(0x4D1C334D294DFA97)
enum {
    MAX_HELLDIVER_EDITS = 24,
    MAX_MAGAZINES = 31,
    MAX_THROWABLES = 99,
    HEALTH_MAP = 0x1030,
    HEALTH_RECORD_STRIDE = 0x1B8,
    HEALTH_EXT_STRIDE = 0x1C
};

/* Default magazine and throwable counts by loadout id, generated from the stats schema at build time. */
typedef struct HelldiverItemField { const char* component; uint32_t offset; } HelldiverItemField;
typedef struct HelldiverItem {
    uint32_t id;
    uint8_t slot, delta, shared, count;
    uint64_t resource;
    int64_t record;
    HelldiverItemField fields[3];
} HelldiverItem;
static const HelldiverItem kHelldiverItems[] = {
#include "smr_helldiver_items.inc"
};

static const HfField kStartingMagazines = HF_FIELD(WeaponMagazine, starting_magazines);
static const HfField kMaxMagazines = HF_FIELD(WeaponMagazine, max_magazines);
static const HfField kThrowableStart = HF_FIELD(Throwable, starting_count);
static const HfField kThrowableMax = HF_FIELD(Throwable, max_count);
static const HfField kThrowableSupply = HF_FIELD(Throwable, count_from_supply);
static const HfField kSprint = HF_FIELD(Avatar, sprint);
static const HfField kStamina = HF_FIELD(Avatar, sprint_duration);
static const HfField kMovement[] = {
    HF_FIELD(Avatar, walk), HF_FIELD(Avatar, jog), HF_FIELD(Avatar, sprint_exhausted), HF_FIELD(Avatar, crouch_aim),
    HF_FIELD(Avatar, crouch_walk), HF_FIELD(Avatar, crouch_jog), HF_FIELD(Avatar, crouch_sprint), HF_FIELD(Avatar, prone),
    HF_FIELD(Avatar, swim),
};

typedef struct HelldiverEdit {
    int stat;
    HfRef target;
    const HfField* field;
    uint32_t flags;
    HfStat key;
    HfValue value;
} HelldiverEdit;
typedef struct HelldiverEffects {
    double primary, secondary, throwables, armor, damage, sprint, stamina, movement;
    int auto_stim, field_medic;
    double health_percent, cooldown_seconds;
} HelldiverEffects;

static SmrHelldiverTuning helldiver_tuning[SMR_PERK_COUNT];
static int helldiver_config_loaded;
static HelldiverEdit helldiver_edits[MAX_HELLDIVER_EDITS];
static size_t helldiver_edit_count;
static int helldiver_applied, helldiver_refused;
static HelldiverEffects helldiver_effects;
static struct {
    uint32_t avatar;
    float poll, since_stim, since_heal;
} helldiver_runtime;

const SmrHelldiverTuning* SmrGetHelldiverTuning(SmrHelldiverPerk perk)
{
    return helldiver_config_loaded && perk > SMR_PERK_NONE && perk < SMR_PERK_COUNT ? &helldiver_tuning[perk] : NULL;
}

/* The summed tier prices of the given perks; returns whether any perk is priced. */
static int HelldiverPrice(const SmrHelldiverPerk perks[SMR_TIER_COUNT], unsigned price[CURRENCY_COUNT])
{
    int any = 0;
    memset(price, 0, sizeof(unsigned) * CURRENCY_COUNT);
    for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
    {
        const SmrHelldiverTuning* tuning = SmrGetHelldiverTuning(perks[tier]);
        if (!tuning) continue;
        any = 1;
        for (size_t i = 0; i < CURRENCY_COUNT; ++i) price[i] += tuning->price[i];
    }
    return any;
}

SmrHelldiverStatus SmrHelldiver(HfMod* mod)
{
    SmrHelldiverStatus status = {{0}};
    AcquireSRWLockShared(&shop_lock);
    int open = upgrade_funds_ready && !shop_failed && TierSelectionOpen();
    for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
    {
        status.available[tier] = open && !helldiver_purchased[tier];
        status.selected[tier] = helldiver_selected[tier];
        status.purchased[tier] = helldiver_purchased[tier];
    }
    memcpy(status.balance, currency_amount, sizeof status.balance);
    ReleaseSRWLockShared(&shop_lock);
    return status;
}

int SmrSelectHelldiver(HfMod* mod, SmrHelldiverTier tier, SmrHelldiverPerk perk)
{
    int on_ship = HF_API(mod)->game_state(mod) == HF_STATE_SHIP;
    AcquireSRWLockExclusive(&shop_lock);
    int allowed = on_ship && tier >= SMR_TIER_PLUS && tier < SMR_TIER_COUNT && TierSelectionOpen() &&
        !helldiver_purchased[tier] &&
        (perk == SMR_PERK_NONE ||
            (upgrade_funds_ready && !shop_failed && SmrGetHelldiverTuning(perk) && SmrPerkTier(perk) == tier));
    if (allowed)
    {
        helldiver_selected[tier] = perk;
    }
    ReleaseSRWLockExclusive(&shop_lock);
    if (!allowed) ShopMessage(mod, "Helldiver upgrade selection unavailable.");
    return allowed;
}

static HelldiverEffects HelldiverPlanEffects(const SmrHelldiverPerk perks[SMR_TIER_COUNT])
{
    HelldiverEffects fx = {1, 1, 1, 1, 1, 1, 1, 1};
    for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
    {
        const SmrHelldiverTuning* t = SmrGetHelldiverTuning(perks[tier]);
        if (!t) continue;
        fx.primary *= 1 + t->primary_ammo_percent / 100;
        fx.secondary *= 1 + t->secondary_ammo_percent / 100;
        fx.throwables *= 1 + t->throwables_percent / 100;
        fx.armor *= 1 + t->armor_percent / 100;
        fx.sprint *= 1 + t->sprint_percent / 100;
        fx.stamina *= 1 + t->stamina_percent / 100;
        fx.movement *= 1 + t->movement_percent / 100;
        if (perks[tier] == SMR_PERK_AUTO_STIM)
        {
            fx.auto_stim = 1;
            fx.health_percent = t->health_percent;
            fx.cooldown_seconds = t->cooldown_seconds;
        }
        if (perks[tier] == SMR_PERK_FIELD_MEDIC)
            fx.field_medic = 1;
    }
    fx.damage = 1 / fx.armor;
    return fx;
}

static const HelldiverItem* HelldiverItemFor(uint32_t id, uint8_t slot)
{
    for (size_t i = 0; id && i < sizeof kHelldiverItems / sizeof kHelldiverItems[0]; ++i)
        if (kHelldiverItems[i].id == id && kHelldiverItems[i].slot == slot) return &kHelldiverItems[i];
    return NULL;
}

static const HfField* HelldiverComponentField(uint8_t slot, uint32_t offset)
{
    const HfField* fields[] = {&kStartingMagazines, &kMaxMagazines, &kThrowableStart, &kThrowableMax, &kThrowableSupply};
    for (size_t i = slot == 2 ? 2 : 0; i < (slot == 2 ? 5u : 2u); ++i)
        if (fields[i]->offset == offset) return fields[i];
    return NULL;
}

static int SameHelldiverEdit(const HelldiverEdit* a, const HelldiverEdit* b)
{
    return a->stat == b->stat && (a->stat ?
        a->key.resource == b->key.resource && a->key.record == b->key.record && a->key.offset == b->key.offset :
        a->target == b->target && a->field == b->field);
}

static int AddHelldiverEdit(HelldiverEdit* edits, size_t* count, const HelldiverEdit* edit)
{
    for (size_t i = 0; i < *count; ++i)
        if (SameHelldiverEdit(&edits[i], edit)) return 1;
    if (*count >= MAX_HELLDIVER_EDITS) return 0;
    edits[(*count)++] = *edit;
    return 1;
}

static int AddCountEdits(HfMod* mod, const HelldiverItem* item, double factor, unsigned max,
    HelldiverEdit* edits, size_t* count)
{
    const HfApi* api = HF_API(mod);
    for (uint8_t f = 0; f < item->count; ++f)
    {
        HelldiverEdit edit = {item->delta};
        HfLiveValue now;
        if (item->delta)
        {
            edit.key = (HfStat){"EntityDelta", item->fields[f].component, item->resource, item->record,
                item->fields[f].offset, HF_TYPE_U32};
            if (!api->get_stat(mod, &edit.key, &now)) return 0;
        }
        else
        {
            edit.target = item->resource;
            edit.flags = item->shared ? HF_LIVE_SHARED : 0;
            if (!(edit.field = HelldiverComponentField(item->slot, item->fields[f].offset)) ||
                !api->get_live(mod, edit.target, edit.field, &now)) return 0;
        }
        double base = now.now.number, value = round(base * factor);
        if (base <= 0) continue;
        value = value < 0 ? 0 : value > max ? max : value;
        if (value == base) continue;
        edit.value = (HfValue)HF_U32(value);
        if (!AddHelldiverEdit(edits, count, &edit)) return 0;
    }
    return 1;
}

static int AddScaledEdit(HfMod* mod, HfRef target, const HfField* field, uint32_t flags, double factor,
    HelldiverEdit* edits, size_t* count)
{
    if (factor == 1) return 1;
    HfLiveValue now;
    if (!HF_API(mod)->get_live(mod, target, field, &now)) return 0;
    double value = now.now.number * factor;
    if (!(value >= 0 && value <= FLT_MAX)) return 0;
    HelldiverEdit edit = {0, target, field, flags};
    edit.value = (HfValue)HF_F32(value);
    return AddHelldiverEdit(edits, count, &edit);
}

static int ReadWeapons(const HfApi* api, uintptr_t loadout, uint32_t weapons[3])
{
    if (!loadout) return 0;
    for (size_t i = 0; i < 3; ++i)
        if (api->read(loadout + i * WEAPON_STRIDE, &weapons[i], sizeof weapons[i])) return 0;
    return 1;
}

static int PlanHelldiver(HfMod* mod, const uint32_t weapons[3], const HelldiverEffects* fx,
    HelldiverEdit* edits, size_t* count)
{
    const double ammo[3] = {fx->primary, fx->secondary, fx->throwables};
    *count = 0;
    for (uint8_t slot = 0; slot < 3; ++slot)
    {
        if (ammo[slot] == 1) continue;
        const HelldiverItem* item = HelldiverItemFor(weapons[slot], slot);
        if (item && !AddCountEdits(mod, item, ammo[slot], slot == 2 ? MAX_THROWABLES : MAX_MAGAZINES, edits, count))
            return 0;
    }
    if (!AddScaledEdit(mod, HELLDIVER_RESOURCE, &kSprint, HF_LIVE_SHARED, fx->sprint * fx->movement, edits, count) ||
        !AddScaledEdit(mod, HELLDIVER_RESOURCE, &kStamina, HF_LIVE_SHARED, fx->stamina, edits, count))
        return 0;
    for (size_t i = 0; i < sizeof kMovement / sizeof kMovement[0]; ++i)
        if (!AddScaledEdit(mod, HELLDIVER_RESOURCE, &kMovement[i], HF_LIVE_SHARED, fx->movement, edits, count))
            return 0;
    return 1;
}

static int HelldiverAny(const SmrHelldiverPerk perks[SMR_TIER_COUNT])
{
    return perks[SMR_TIER_PLUS] || perks[SMR_TIER_ELITE] || perks[SMR_TIER_SUPER];
}

/* Checkout check: every selected perk is configured for its tier. */
static int CanApplyHelldiver(HfMod* mod, const SmrHelldiverPerk perks[SMR_TIER_COUNT])
{
    for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
        if (perks[tier] && (!SmrGetHelldiverTuning(perks[tier]) || SmrPerkTier(perks[tier]) != tier)) return 0;
    return 1;
}

static void ResetHelldiver(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    while (helldiver_edit_count)
    {
        const HelldiverEdit* edit = &helldiver_edits[--helldiver_edit_count];
        if (edit->stat) api->set_stat(mod, &edit->key, NULL, 0);
        else api->set_live(mod, edit->target, edit->field, NULL, edit->flags);
    }
    helldiver_applied = 0;
    memset(&helldiver_effects, 0, sizeof helldiver_effects);
    memset(&helldiver_runtime, 0, sizeof helldiver_runtime);
}

static void ApplyHelldiver(HfMod* mod, uintptr_t loadout)
{
    SmrHelldiverPerk perks[SMR_TIER_COUNT];
    AcquireSRWLockShared(&shop_lock);
    memcpy(perks, helldiver_purchased, sizeof perks);
    ReleaseSRWLockShared(&shop_lock);
    if (!HelldiverAny(perks) || helldiver_applied || helldiver_refused) return;
    const HfApi* api = HF_API(mod);
    HelldiverEffects fx = HelldiverPlanEffects(perks);
    uint32_t weapons[3];
    HelldiverEdit edits[MAX_HELLDIVER_EDITS];
    size_t count;
    if (ReadWeapons(api, loadout, weapons) && PlanHelldiver(mod, weapons, &fx, edits, &count))
    {
        size_t i = 0;
        for (; i < count; ++i)
        {
            const HelldiverEdit* edit = &edits[i];
            if (!(edit->stat ? api->set_stat(mod, &edit->key, &edit->value, 0) :
                    api->set_live(mod, edit->target, edit->field, &edit->value, edit->flags)))
                break;
            helldiver_edits[helldiver_edit_count++] = *edit;
        }
        if (i == count)
        {
            helldiver_applied = 1;
            helldiver_effects = fx;
            helldiver_runtime.since_stim = (float)fx.cooldown_seconds;
            api->log(mod, "Helldiver upgrade active: plus=%d elite=%d super=%d; %u fields updated.",
                perks[SMR_TIER_PLUS], perks[SMR_TIER_ELITE], perks[SMR_TIER_SUPER], (unsigned)count);
            return;
        }
    }
    ResetHelldiver(mod);
    helldiver_refused = 1;
    ShopMessage(mod, "Helldiver upgrade could not be applied; purchase retained for a later mission.");
}

/* Ends this mission's Helldiver upgrade; returns whether it was active (and so is consumed). */
static int FinishHelldiver(HfMod* mod)
{
    int applied = helldiver_applied;
    ResetHelldiver(mod);
    helldiver_refused = 0;
    return applied;
}

static void HelldiverWeapon(HfMod* mod, const HfWeaponEvent* event, void* user)
{
    if (helldiver_runtime.avatar != event->avatar)
        helldiver_runtime.since_heal = 0;
    helldiver_runtime.avatar = event->avatar;
}

static void HelldiverDamagePre(HfMod* mod, HfDamagePreEvent* event, void* user)
{
    double factor = helldiver_effects.damage;
    if (!helldiver_applied || factor == 1 || !helldiver_runtime.avatar ||
        event->damage.target != helldiver_runtime.avatar || event->damage.amount <= 0 || event->cancel)
        return;
    double amount = round(event->damage.amount * factor);
    if (amount < 1)
    {
        if (factor <= 0) event->cancel = 1;
        else event->damage.amount = 1;
        return;
    }
    event->damage.amount = amount > INT32_MAX ? INT32_MAX : (int32_t)amount;
}

/* The avatar's health, maximum and life (0 alive) from the HealthManager (hellforge_globals.h). */
static int HelldiverHealth(HfMod* mod, uint32_t entity, int32_t* health, int32_t* max, uint32_t* life)
{
    const HfApi* api = HF_API(mod);
    uintptr_t manager = (uintptr_t)hf_global_HealthManager(mod), buckets, descriptors, records, ext, descriptor;
    uint32_t capacity, empty, multiplier, count, index = UINT32_MAX, named;
    if (!manager || !entity ||
        api->read(manager + HEALTH_MAP, &buckets, sizeof buckets) ||
        api->read(manager + HEALTH_MAP + 8, &capacity, sizeof capacity) ||
        api->read(manager + HEALTH_MAP + 12, &empty, sizeof empty) ||
        api->read(manager + HEALTH_MAP + 16, &multiplier, sizeof multiplier) ||
        api->read(manager + offsetof(HfHealthManager, count), &count, sizeof count) ||
        !buckets || !capacity || capacity > 1048576)
        return 0;
    uint32_t start = entity * multiplier, probes = capacity < 4096 ? capacity : 4096;
    for (uint32_t i = 0; i < probes; ++i)
    {
        uintptr_t at = buckets + (uintptr_t)((start + i) % capacity) * 8;
        uint32_t key;
        if (api->read(at, &key, sizeof key) || key == empty) return 0;
        if (key == entity)
        {
            if (api->read(at + 4, &index, sizeof index)) return 0;
            break;
        }
    }
    return index < count &&
        !api->read(manager + offsetof(HfHealthManager, descriptors), &descriptors, sizeof descriptors) &&
        !api->read(manager + offsetof(HfHealthManager, records), &records, sizeof records) &&
        !api->read(manager + offsetof(HfHealthManager, ext), &ext, sizeof ext) &&
        !api->read(descriptors + (uintptr_t)index * 8, &descriptor, sizeof descriptor) && descriptor &&
        !api->read(descriptor + 8, &named, sizeof named) && named == entity &&
        !api->read(records + (uintptr_t)index * HEALTH_RECORD_STRIDE + 0x14, health, sizeof *health) &&
        !api->read(records + (uintptr_t)index * HEALTH_RECORD_STRIDE + 0x19C, life, sizeof *life) &&
        !api->read(ext + (uintptr_t)index * HEALTH_EXT_STRIDE + 0x14, max, sizeof *max) && *max > 0;
}

static void HelldiverTick(HfMod* mod, float dt)
{
    const HfApi* api = HF_API(mod);
    uint32_t avatar = helldiver_runtime.avatar;
    if (!helldiver_applied || (!helldiver_effects.auto_stim && !helldiver_effects.field_medic) ||
        api->game_state(mod) != HF_STATE_MISSION || dt <= 0)
        return;
    helldiver_runtime.since_stim += dt;
    helldiver_runtime.poll += dt;
    if (helldiver_runtime.poll < 0.1f) return;
    float elapsed = helldiver_runtime.poll;
    helldiver_runtime.poll = 0;
    if (!avatar) return;
    int32_t health, max;
    uint32_t life;
    int alive = HelldiverHealth(mod, avatar, &health, &max, &life) && life == 0 && health > 0;
    if (helldiver_effects.auto_stim && alive && helldiver_runtime.since_stim >= helldiver_effects.cooldown_seconds &&
        health * 100.0 <= max * helldiver_effects.health_percent)
    {
        uint32_t done = api->heal(mod, (float)max);
        if (done)
        {
            helldiver_runtime.since_stim = 0;
            api->log(mod, "Auto-stim: health %d/%d.", health, max);
        }
    }
    if (!helldiver_effects.field_medic) return;
    if (!alive)
    {
        helldiver_runtime.since_heal = 0;
        return;
    }
    helldiver_runtime.since_heal += elapsed;
    float amount = floorf(helldiver_runtime.since_heal);
    if (amount >= 1)
    {
        helldiver_runtime.since_heal -= amount;
        if (health < max && !api->heal(mod, amount))
            api->log(mod, "Field medic: health regeneration refused.");
    }
}
