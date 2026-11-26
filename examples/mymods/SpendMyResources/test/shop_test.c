#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/spend_my_resources.c"

struct HfMod { const HfApi* api; };
static HfApi api;
static HfMod mod = {&api};
static unsigned char *image, *manager, *history, *profile;
static unsigned char picker_menu[READY_SCREEN + sizeof(uintptr_t)];
static unsigned char picker_screen[READY_CARD + READY_PEER + sizeof(uint64_t)];
static HfLocalUser local;
static HfLocalUser* local_pointer = &local;
static uintptr_t slot_players, slot_mode;
static int package_state, grant_calls, refuse_grant;
static uint32_t game_state = HF_STATE_SHIP;
static int local_ready = 1, pins_match = 1, remove_failed;
static int wallet_exists, wallet_refused, wallet_writes;
static SmrWallet stored_wallet;
#define saved_currency (stored_wallet.amount)
#define saved_wallet (saved_currency[0])
static int prices_refused, subscription_calls;
static int input_result = 1, hook_refused;
static uintptr_t last_button, last_input, unreadable_at;
static int add_failed, add_calls;
static uint32_t session_role, refused_type;
static double orbital_cooldown = 100, barrage_cooldown = 50, support_cooldown = 80, resupply_cooldown = 160;
static double eagle_cooldown = 8, second_eagle_cooldown = 10, eagle_call_in = 3, second_eagle_call_in = 4;
static double sentry_cooldown = 90, rearm_cooldown = 120, sentry_uses = 4, support_uses = 3;
static const HfField* refused_field;
static int live_sets;

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
static size_t PriceIndex(const char* name)
{
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if (!strcmp(kLoadoutPrices[i].name, name)) return i;
    CHECK(0 && "Missing expected restriction.");
    return PRICE_COUNT;
}

static int Read(uintptr_t at, void* out, size_t size) { if (!at || at == unreadable_at) return 1; memcpy(out, (const void*)at, size); return 0; }
static uintptr_t Base(const char* name, size_t* size) { return !strcmp(name, "game.dll") ? (uintptr_t)image : 0; }
static const char* Build(void) { return "25480438"; }
static uint32_t State(HfMod* unused) { return game_state; }
static void Session(HfMod* unused, HfSession* out) { out->role = session_role; }
static double* Cooldown(uint32_t type)
{
    switch (type)
    {
        case HF_STRATAGEM_ORBITAL_PRECISION_STRIKE: return &orbital_cooldown;
        case HF_STRATAGEM_ORBITAL_WALKING_BARRAGE: return &barrage_cooldown;
        case HF_STRATAGEM_MS11_SOLO_SILO: return &support_cooldown;
        case HF_STRATAGEM_RESUPPLY: return &resupply_cooldown;
        case HF_STRATAGEM_EAGLE_AIRSTRIKE: return &eagle_cooldown;
        case HF_STRATAGEM_EAGLE_500KG_BOMB: return &second_eagle_cooldown;
        case HF_STRATAGEM_AG16_GATLING_SENTRY: return &sentry_cooldown;
        case HF_STRATAGEM_EAGLE_REARM: return &rearm_cooldown;
        default: return NULL;
    }
}
/* Helldiver upgrade targets, by target and field or stat offset. */
typedef struct HelldiverValue { int stat; uint64_t target; const HfField* field; int64_t record; uint32_t offset; double now; } HelldiverValue;
static HelldiverValue helldiver_values[64];
static size_t helldiver_value_count;
static int helldiver_stat_sets_before_refusal = -1;
static const HfField fixture_stim_duration = HF_FIELD(StatusEffectSettings, duration);
static int HelldiverFixtureField(const HfField* field)
{
    if (field == &kStartingMagazines || field == &kMaxMagazines || field == &kThrowableStart || field == &kThrowableMax ||
        field == &kThrowableSupply || field == &kSprint || field == &kStamina) return 1;
    for (size_t i = 0; i < sizeof kMovement / sizeof kMovement[0]; ++i)
        if (field == &kMovement[i]) return 1;
    return 0;
}
static double HelldiverBase(const HelldiverValue* key)
{
    if (key->stat || key->field == &kStartingMagazines || key->field == &kMaxMagazines) return 6;
    if (key->field == &kThrowableStart || key->field == &kThrowableMax || key->field == &kThrowableSupply) return 4;
    return 10;
}
static HelldiverValue* HelldiverFixtureValue(int stat, uint64_t target, const HfField* field, int64_t record, uint32_t offset)
{
    for (size_t i = 0; i < helldiver_value_count; ++i)
    {
        HelldiverValue* v = &helldiver_values[i];
        if (v->stat == stat && v->target == target && v->field == field && v->record == record && v->offset == offset) return v;
    }
    CHECK(helldiver_value_count < sizeof helldiver_values / sizeof helldiver_values[0]);
    HelldiverValue* v = &helldiver_values[helldiver_value_count++];
    *v = (HelldiverValue){stat, target, field, record, offset};
    v->now = HelldiverBase(v);
    return v;
}
static double HelldiverNow(uint64_t target, const HfField* field) { return HelldiverFixtureValue(0, target, field, 0, 0)->now; }
static double HelldiverStatNow(const HelldiverItem* item, int f)
{
    return HelldiverFixtureValue(1, item->resource, NULL, item->record, item->fields[f].offset)->now;
}
static int GetStat(HfMod* unused, const HfStat* stat, HfLiveValue* out)
{
    CHECK(!strcmp(stat->kind, "EntityDelta") && stat->type == HF_TYPE_U32);
    out->now.number = HelldiverFixtureValue(1, stat->resource, NULL, stat->record, stat->offset)->now;
    return HF_APPLIES_RESPAWN;
}
static int SetStat(HfMod* unused, const HfStat* stat, const HfValue* value, uint32_t flags)
{
    if (value && helldiver_stat_sets_before_refusal >= 0)
    {
        if (!helldiver_stat_sets_before_refusal) return 0;
        --helldiver_stat_sets_before_refusal;
    }
    HelldiverValue* v = HelldiverFixtureValue(1, stat->resource, NULL, stat->record, stat->offset);
    if (value) CHECK(value->type == HF_TYPE_U32);
    v->now = value ? value->number : HelldiverBase(v);
    live_sets += value != NULL;
    return HF_APPLIES_RESPAWN;
}
static int GetLive(HfMod* mod, HfRef type, const HfField* field, HfLiveValue* out)
{
    CHECK(type != 27 || strcmp(field->component, fixture_stim_duration.component) || field->offset != fixture_stim_duration.offset);
    if (HelldiverFixtureField(field))
    {
        out->now.number = HelldiverNow(type, field);
        return HF_APPLIES_RESPAWN;
    }
    double* cooldown = Cooldown((uint32_t)type);
    if (!cooldown) return 0;
    if (field == &kCooldown) out->now.number = *cooldown;
    else if (field == &kDelivery)
        out->now.number = cooldown == &orbital_cooldown ? DELIVERY_ORBITAL :
            cooldown == &barrage_cooldown ? DELIVERY_BARRAGE :
            (cooldown == &eagle_cooldown || cooldown == &second_eagle_cooldown) ? DELIVERY_EAGLE : 2;
    else if (field == &kHudType)
        out->now.number = cooldown == &support_cooldown ? HUD_SUPPORT :
            cooldown == &sentry_cooldown ? HUD_DEFENSIVE : 0;
    else if (field == &kCallIn) out->now.number = cooldown == &eagle_cooldown ? eagle_call_in : second_eagle_call_in;
    else if (field == &kUses)
        out->now.number = cooldown == &sentry_cooldown ? sentry_uses : support_uses;
    else return 0;
    return field == &kUses ? HF_APPLIES_MISSION : HF_APPLIES_NOW;
}
static int SetLive(HfMod* mod, HfRef type, const HfField* field, const HfValue* value, uint32_t flags)
{
    CHECK(type != 27 || strcmp(field->component, fixture_stim_duration.component) || field->offset != fixture_stim_duration.offset);
    if (HelldiverFixtureField(field))
    {
        HelldiverValue* v = HelldiverFixtureValue(0, type, field, 0, 0);
        CHECK(type != HELLDIVER_RESOURCE || flags == HF_LIVE_SHARED);
        if (value && type == refused_type) return 0;
        if (value) CHECK(value->type == field->type);
        v->now = value ? value->number : HelldiverBase(v);
        live_sets += value != NULL;
        return HF_APPLIES_RESPAWN;
    }
    double* cooldown = Cooldown((uint32_t)type);
    CHECK(cooldown && !flags && type != HF_STRATAGEM_RESUPPLY);
    if (value && type == refused_type && (!refused_field || field == refused_field)) return 0;
    if (value) CHECK(value->type == field->type);
    if (field == &kCooldown)
        *cooldown = value ? value->number : cooldown == &orbital_cooldown ? 100 : cooldown == &barrage_cooldown ? 50 :
            cooldown == &support_cooldown ? 80 : cooldown == &sentry_cooldown ? 90 :
            cooldown == &eagle_cooldown ? 8 : cooldown == &second_eagle_cooldown ? 10 : 120;
    else if (field == &kCallIn && cooldown == &eagle_cooldown) eagle_call_in = value ? value->number : 3;
    else if (field == &kCallIn && cooldown == &second_eagle_cooldown) second_eagle_call_in = value ? value->number : 4;
    else if (field == &kUses && cooldown == &sentry_cooldown) sentry_uses = value ? value->number : 4;
    else if (field == &kUses && cooldown == &support_cooldown) support_uses = value ? value->number : 3;
    else CHECK(0 && "Unexpected edit");
    live_sets += value != NULL;
    return field == &kUses ? HF_APPLIES_MISSION : HF_APPLIES_NOW;
}
static int Matches(uintptr_t at, const char* pin) { return pins_match; }
static unsigned char health_manager[0x1100], *health_manager_pointer = health_manager;
static uintptr_t Global(HfMod* unused, const char* name)
{
    if (!strcmp(name, "PlayerManager")) return (uintptr_t)&slot_players;
    if (!strcmp(name, "GameMode")) return (uintptr_t)&slot_mode;
    if (!strcmp(name, "HealthManager")) return (uintptr_t)&health_manager_pointer;
    return local_ready ? (uintptr_t)&local_pointer : 0;
}
static int Subscribe(HfMod* unused, uint32_t kind, HfListener listener, void* data, int32_t priority) { subscription_calls++; return 1; }
static int OnTick(HfMod* unused, HfTickCallback callback, void* data) { return 1; }
static int OriginalInput(uintptr_t button, uintptr_t input)
{
    last_button = button;
    last_input = input;
    return input_result;
}
static int Hook(HfMod* unused, uintptr_t target, void* detour, void** original)
{
    CHECK(target == (uintptr_t)image + READY_INPUT && detour == (void*)ReadyInput);
    if (hook_refused) return 1;
    *original = (void*)OriginalInput;
    return 0;
}
static const char* Logs(HfMod* unused) { return "fixture"; }
static void Log(HfMod* unused, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    puts("");
    va_end(arguments);
}
static unsigned Amount(uint32_t id)
{
    unsigned amount = 0;
    CHECK(ItemRecord(&api, (uintptr_t)manager, id, &amount));
    return amount;
}
static void SetAmount(uint32_t id, unsigned amount)
{
    unsigned previous;
    uintptr_t record = ItemRecord(&api, (uintptr_t)manager, id, &previous);
    CHECK(record);
    memcpy((void*)(record + 0xB0), &amount, 4);
    uint32_t state = amount ? 2 : 1;
    memcpy((void*)(record + 0x14), &state, 4);
}
static void Remove(uintptr_t unused, uint32_t id, uint32_t amount)
{
    int currency = 0;
    for (size_t i = 0; i < CURRENCY_COUNT; ++i) currency |= id == kCurrencies[i].id;
    CHECK(currency);
    if (remove_failed) return;
    unsigned before = Amount(id);
    SetAmount(id, before > amount ? before - amount : 0);
}
static void Add(uintptr_t unused, uint32_t id, uint32_t amount, uint32_t source, const char* text, int extra)
{
    CHECK(!source && !text && !extra);
    add_calls++;
    if (add_failed == 1) return;
    unsigned before;
    uintptr_t record = ItemRecord(&api, (uintptr_t)manager, id, &before);
    CHECK(record);
    unsigned cap;
    memcpy(&cap, (const void*)(record + 0x18), sizeof cap);
    uint64_t after = (uint64_t)before + amount - (add_failed == 2 ? 1 : 0);
    unsigned limit = cap ? cap : UINT_MAX;
    SetAmount(id, after > limit ? limit : (unsigned)after);
}

int SmrOpenLedger(const char* logs, const SmrWallet* initial, SmrWallet* saved, char* result, size_t size)
{
    CHECK(!strcmp(logs, "fixture"));
    if (!wallet_exists)
    {
        stored_wallet = *initial;
        wallet_exists = 1;
    }
    *saved = stored_wallet;
    return 1;
}

int SmrWriteLedger(const SmrWallet* wallet, char* result, size_t size)
{
    CHECK(wallet_exists);
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
        CHECK(wallet->amount[i] == Amount(kCurrencies[i].id));
    ++wallet_writes;
    snprintf(result, size, "Wallet fixture %s.", wallet_refused ? "failed" : "saved");
    if (wallet_refused) return 0;
    stored_wallet = *wallet;
    return 1;
}
int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size)
{
    tuning[SMR_UPGRADE_ORBITAL] = (SmrUpgradeTuning){.price = {2000, 10, 5, 1, 10}, .cooldown_percent = -66, .other_cooldown_percent = 50};
    tuning[SMR_UPGRADE_EAGLE] = (SmrUpgradeTuning){.price = {2000, 10, 5, 1, 10}, .other_cooldown_percent = 50,
        .cooldown_seconds = 20, .call_in_extra_seconds = 2, .rearm_seconds = 30};
    tuning[SMR_UPGRADE_DEFENSIVE] = (SmrUpgradeTuning){.price = {2000, 10, 5, 1, 10}, .cooldown_percent = -75, .other_cooldown_percent = 50};
    tuning[SMR_UPGRADE_SUPPORT] = (SmrUpgradeTuning){.price = {2000, 10, 5, 1, 13}, .cooldown_percent = -50, .other_cooldown_percent = 50};
    tuning[SMR_UPGRADE_EXTRA_SLOT] = (SmrUpgradeTuning){.price = {[REQUISITION_SLIPS] = 2000, [FORGE_CREDITS] = 50},
        .other_cooldown_percent = 10};
    return 1;
}
int SmrLoadHelldiverConfig(SmrHelldiverTuning tuning[SMR_PERK_COUNT], char* result, size_t size)
{
    const SmrHelldiverTuning plus = {.price = {[COMMON_SAMPLES] = 50}}, elite = {.price = {[RARE_SAMPLES] = 25}},
        super = {.price = {[SUPER_SAMPLES] = 5}};
    tuning[SMR_PERK_PRIMARY_TRADEOFF] = plus;
    tuning[SMR_PERK_PRIMARY_TRADEOFF].primary_ammo_percent = -50;
    tuning[SMR_PERK_PRIMARY_TRADEOFF].secondary_ammo_percent = 50;
    tuning[SMR_PERK_SECONDARY_TRADEOFF] = plus;
    tuning[SMR_PERK_SECONDARY_TRADEOFF].secondary_ammo_percent = -50;
    tuning[SMR_PERK_SECONDARY_TRADEOFF].primary_ammo_percent = 50;
    tuning[SMR_PERK_LIGHT_KIT] = plus;
    tuning[SMR_PERK_LIGHT_KIT].throwables_percent = -50;
    tuning[SMR_PERK_LIGHT_KIT].armor_percent = -10;
    tuning[SMR_PERK_LIGHT_KIT].primary_ammo_percent = 33;
    tuning[SMR_PERK_LIGHT_KIT].secondary_ammo_percent = 33;
    tuning[SMR_PERK_ARMORED] = elite;
    tuning[SMR_PERK_ARMORED].armor_percent = 25;
    tuning[SMR_PERK_ARMORED].primary_ammo_percent = -10;
    tuning[SMR_PERK_ARMORED].secondary_ammo_percent = -10;
    tuning[SMR_PERK_SPRINTER] = elite;
    tuning[SMR_PERK_SPRINTER].sprint_percent = 10;
    tuning[SMR_PERK_SPRINTER].stamina_percent = -25;
    tuning[SMR_PERK_ENDURANCE] = elite;
    tuning[SMR_PERK_ENDURANCE].stamina_percent = 25;
    tuning[SMR_PERK_ENDURANCE].sprint_percent = -10;
    tuning[SMR_PERK_AUTO_STIM] = super;
    tuning[SMR_PERK_AUTO_STIM].health_percent = 20;
    tuning[SMR_PERK_AUTO_STIM].cooldown_seconds = 30;
    tuning[SMR_PERK_AUTO_STIM].armor_percent = -10;
    tuning[SMR_PERK_FIELD_MEDIC] = super;
    tuning[SMR_PERK_FIELD_MEDIC].injury_seconds = 2;
    tuning[SMR_PERK_FIELD_MEDIC].movement_percent = -10;
    tuning[SMR_PERK_FIELD_MEDIC].armor_percent = -10;
    return 1;
}

int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size)
{
    if (prices_refused)
    {
        snprintf(result, size, "Synthetic invalid price file.");
        return 0;
    }
    CHECK(count == PRICE_COUNT);
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(!strcmp(names[i], kLoadoutPrices[i].name));
        /* Use the fixture's prices, independently of the shipped prices.json. */
        prices[i] = i == PriceIndex("R-36 Eruptor") ? 750 :
            i == PriceIndex("MS-11 Solo Silo") ? 2000 : 500;
    }
    return 1;
}
static size_t EnemyNamed(const char* name)
{
    for (size_t i = 0; i < ENEMY_COUNT; ++i)
        if (!strcmp(kEnemies[i].name, name)) return i;
    CHECK(0 && "Missing expected enemy.");
    return 0;
}
int SmrLoadKillRewards(const char* const* names, unsigned* values, size_t count, char* result, size_t size)
{
    CHECK(count == ENEMY_COUNT);
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(!strcmp(names[i], kEnemies[i].name));
        values[i] = !strcmp(names[i], "Bile Titan - Strider") ? 500 : !strcmp(names[i], "Charger") ? 100 :
            !strcmp(names[i], "Stalker") ? 0 : SMR_KILL_DEFAULT_VALUE;
    }
    return 1;
}
static int GearCheckout(unsigned* total, int* pending)
{
    unsigned price[CURRENCY_COUNT];
    SmrCheckoutAction action = SmrCheckout(total, price, pending);
    for (size_t i = 0; i < CURRENCY_COUNT; ++i) CHECK(!price[i]);
    return action == SMR_CHECKOUT_PURCHASE;
}
static void Tick(void) { ShopTick(&mod, 1.0f / 60.0f, NULL); }
static void WaitFor(int state)
{
    ULONGLONG deadline = GetTickCount64() + 10000;
    for (;;)
    {
        Tick();
        AcquireSRWLockShared(&shop_lock);
        int now = charge.state;
        ReleaseSRWLockShared(&shop_lock);
        if (now == state) break;
        CHECK(GetTickCount64() < deadline);
        Sleep(5);
    }
}
static void Put32(unsigned char* at, uint32_t value) { memcpy(at, &value, 4); }
static void PutPointer(unsigned offset, void* pointer) { memcpy(image + offset, &pointer, sizeof pointer); }
static unsigned char* PickerCard(void) { return picker_screen + READY_CARD; }
static void SetPickerSelection(unsigned char* loadout)
{
    memcpy(PickerCard() + READY_SELECTION, &loadout, sizeof loadout);
}
static void Pick(uint32_t primary, uint32_t stratagem)
{
    Put32(profile + 0x40, primary);
    Put32(history + LOADOUT, primary);
    Put32(history + LOADOUT + WEAPON_STRIDE, HF_ITEM_P2_PEACEMAKER);
    Put32(history + LOADOUT + 2 * WEAPON_STRIDE, HF_ITEM_G12_HIGH_EXPLOSIVE);
    Put32(history + LOADOUT + STRATAGEM_COUNT, stratagem ? 1 : 0);
    Put32(history + LOADOUT + STRATAGEM_SLOTS, stratagem);
    Put32(profile + 0x4C, stratagem ? 0x4FB52679u : 0);
}

static void OpenCheckout(void)
{
    if (CheckoutGate() == CHECKOUT_OPEN) return;
    SmrFinishCheckout(0);
    InterlockedExchange(&checkout_gate, CHECKOUT_REQUESTED);
    CHECK(SmrTakeCheckoutRequest());
}

static int BuyCheckout(void) { return SmrConfirmCheckout(&mod, SMR_CHECKOUT_PURCHASE); }

static int BuyItem(size_t i)
{
    if (i >= PRICE_COUNT) return 0;
    if (charge.state == CHARGE_IDLE)
    {
        OpenCheckout();
        Pick(i == 0 ? HF_ITEM_R36_ERUPTOR : HF_ITEM_AR23_LIBERATOR,
            i == PriceIndex("MS-11 Solo Silo") ? HF_STRATAGEM_MS11_SOLO_SILO : 0);
        checkout_equipped = 0;
        checkout_valid = ReadEquippedItems(&mod, &checkout_equipped);
    }
    return BuyCheckout();
}

static int ReadyWithoutPurchase(uintptr_t button, uintptr_t input)
{
    int result = ReadyInput(button, input);
    if (!SmrTakeCheckoutRequest()) return result;
    CHECK(result == 0);
    if (!SmrConfirmCheckout(&mod, SMR_CHECKOUT_CONTINUE))
    {
        SmrFinishCheckout(0);
        return 0;
    }
    SmrFinishCheckout(1);
    return ReadyInput(button, input);
}

static void TestReadyGate(void)
{
    size_t silo = PriceIndex("MS-11 Solo Silo");
    uintptr_t button = (uintptr_t)PickerCard() + READY_BUTTON;
    Pick(HF_ITEM_AR23_LIBERATOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Put32(history + LOADOUT + STRATAGEM_COUNT, 4);
    unsigned char before[LOADOUT + STRATAGEM_COUNT + 4];
    memcpy(before, history, sizeof before);
    unsigned balance_before = wallet;
    CHECK(!ReadyInput(button, 1234));
    CHECK(SmrTakeCheckoutRequest() && !SmrTakeCheckoutRequest());
    CHECK(!ReadyInput(button, 1234) && !SmrTakeCheckoutRequest()); /* Can't bypass or reopen the active shop. */
    unsigned total;
    int pending;
    CHECK(GearCheckout(&total, &pending) && total == kLoadoutPrices[silo].requisition && !pending);
    CHECK(!SmrConfirmCheckout(&mod, SMR_CHECKOUT_CONTINUE)); /* Gear access is still unpaid. */
    SmrFinishCheckout(0);
    CHECK(!SmrTakeCheckoutRequest());
    CHECK(!ReadyInput(button, 1234) && SmrTakeCheckoutRequest()); /* Cancel requires a new press. */
    SmrFinishCheckout(0);
    CHECK(!memcmp(before, history, sizeof before) && wallet == balance_before && !access[0] && !access[silo]);
    CHECK(last_button == button && last_input == 1234);
    CHECK(*(uint32_t*)(history + LOADOUT + STRATAGEM_COUNT) == 4);
    access[silo] = 1;
    CHECK(ReadyWithoutPurchase(button, 1234) == 1);
    access[silo] = 0;
    CHECK(ReadyInput(button + 8, 1234) == 1);
    Put32(picker_menu, 0);
    CHECK(ReadyInput(button, 1234) == 1);
    Put32(picker_menu, 11);
    input_result = 0;
    CHECK(!ReadyInput(button, 1234));
    input_result = 2;
    CHECK(ReadyInput(button, 1234) == 2);
    input_result = 1;
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    CHECK(ReadyWithoutPurchase(button, 1234) == 1);
    Put32(history + RECORD_STRIDE + LOADOUT + STRATAGEM_COUNT, 1);
    Put32(history + RECORD_STRIDE + LOADOUT + STRATAGEM_SLOTS, HF_STRATAGEM_MS11_SOLO_SILO);
    CHECK(ReadyWithoutPurchase(button, 1234) == 1);
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    CHECK(!ReadyWithoutPurchase(button, 1234));
    access[0] = 1;
    CHECK(ReadyWithoutPurchase(button, 1234) == 1);
    access[0] = 0;
    local_ready = 0;
    CHECK(!ReadyInput(button, 1234));
    local_ready = 1;
    unreadable_at = (uintptr_t)history + LOADOUT + STRATAGEM_COUNT;
    CHECK(!ReadyInput(button, 1234) && !SmrTakeCheckoutRequest());
    unreadable_at = 0;
    Put32(history + LOADOUT + STRATAGEM_COUNT, MAX_STRATAGEMS + 1);
    CHECK(!ReadyInput(button, 1234) && !SmrTakeCheckoutRequest());
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    access[0] = 1;
    CHECK(!ReadyInput(button, 1234) && SmrTakeCheckoutRequest());
    CHECK(SmrConfirmCheckout(&mod, SMR_CHECKOUT_CONTINUE) && SmrCheckoutComplete());
    input_result = 0;
    CHECK(!ReadyInput(button, 1234)); /* Still awaiting checkout closure. */
    SmrFinishCheckout(1);
    CHECK(!ReadyInput(button + 8, 1234) && CheckoutGate() == CHECKOUT_RESUME);
    CHECK(ReadyInput(button, 1234) == 1 && CheckoutGate() == CHECKOUT_IDLE);
    CHECK(!ReadyInput(button, 1234)); /* Never toggle Ready twice. */
    OpenCheckout();
    CHECK(SmrConfirmCheckout(&mod, SMR_CHECKOUT_CONTINUE));
    SmrFinishCheckout(1);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    CHECK(!ReadyInput(button, 1234) && CheckoutGate() == CHECKOUT_IDLE);
    OpenCheckout();
    CHECK(SmrConfirmCheckout(&mod, SMR_CHECKOUT_CONTINUE));
    SmrFinishCheckout(1);
    Put32(picker_menu, 0);
    CHECK(!ReadyInput(button, 1234) && CheckoutGate() == CHECKOUT_IDLE);
    Put32(picker_menu, 11);
    access[0] = 0;
    input_result = 1;
    ready_input_original = NULL;
    pins_match = 0;
    CHECK(!InstallReadyGate(&mod) && !ready_input_original);
    pins_match = 1;
    hook_refused = 1;
    CHECK(!InstallReadyGate(&mod) && !ready_input_original);
    hook_refused = 0;
    CHECK(InstallReadyGate(&mod));
    puts("PASS Ready gate: unpurchased selections blocked; purchased, other-player and unrelated inputs preserved");
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
}

static void TestLoadoutRefresh(void)
{
    SmrLoadoutQuote quote;
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    CHECK(SmrQuoteLoadout(&mod, &quote) && quote.total == 2750);

    /* The picker reopens before any new game tick or change to the committed PlayerHistory. */
    Put32(picker_menu, 0);
    CHECK(!SmrQuoteLoadout(&mod, &quote));
    SetPickerSelection(NULL);
    Put32(picker_menu, 11);
    CHECK(SmrLoadoutOpen(&mod) && !SmrQuoteLoadout(&mod, &quote) && !quote.count);
    unsigned char current[STRATAGEM_COUNT + sizeof(uint32_t)] = {0};
    Put32(current, HF_ITEM_AR23_LIBERATOR);
    SetPickerSelection(current);
    CHECK(SmrQuoteLoadout(&mod, &quote) && quote.total == 0 && quote.count == 0);
    Put32(current + STRATAGEM_COUNT, 1);
    Put32(current + STRATAGEM_SLOTS, HF_STRATAGEM_EAGLE_500KG_BOMB);
    CHECK(SmrQuoteLoadout(&mod, &quote) && quote.total == 500 && quote.count == 1 &&
        !strcmp(quote.items[0].name, "Eagle 500kg Bomb"));
    const uint64_t other = local.peer + 1;
    memcpy(PickerCard() + READY_PEER, &other, sizeof other);
    CHECK(!SmrQuoteLoadout(&mod, &quote) && !quote.count);
    memcpy(PickerCard() + READY_PEER, &local.peer, sizeof local.peer);

    uintptr_t button = (uintptr_t)PickerCard() + READY_BUTTON;
    CHECK(!ReadyInput(button, 1234) && SmrTakeCheckoutRequest());
    unsigned total;
    int pending;
    CHECK(GearCheckout(&total, &pending) && total == 500 && !pending);
    CHECK(BuyCheckout());
    Tick();
    size_t bomb = PriceIndex("Eagle 500kg Bomb");
    CHECK(SmrCheckoutComplete() && access[bomb] && !access[0] && !access[PriceIndex("MS-11 Solo Silo")] &&
        saved_wallet == 4500 && Amount(REQUISITION) == 4500 && wallet_writes == 1);
    SmrFinishCheckout(0);

    access[bomb] = 0;
    SetAmount(REQUISITION, 5000);
    SetPickerSelection(history + LOADOUT);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    Tick();
    wallet_writes = 0;
    puts("PASS loadout refresh: open picker quotes current selections immediately and checkout ignores stale mission history");
}

static void ResetExchange(void)
{
    SmrFinishCheckout(0);
    const unsigned amounts[] = {10000, 100, 100, 50, 100}, caps[] = {50000, 500, 250, 100, 0};
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        SetAmount(kCurrencies[i].id, amounts[i]);
        unsigned ignored;
        uintptr_t record = ItemRecord(&api, (uintptr_t)manager, kCurrencies[i].id, &ignored);
        Put32((unsigned char*)record + 0x18, caps[i]);
    }
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    shop_failed = remove_failed = add_failed = wallet_refused = 0;
    game_state = HF_STATE_SHIP;
    Tick();
    wallet_writes = add_calls = 0;
}

static void TestExchanges(void)
{
    unsigned debit, credit;
    const unsigned points[] = {1, 50, 100, 200, 750};
    CHECK(CURRENCY_COUNT == 5);
    for (size_t i = 0; i < CURRENCY_COUNT; ++i) CHECK(kCurrencies[i].points == points[i]);
    for (size_t from = 0; from < CURRENCY_COUNT; ++from)
        for (size_t to = 0; to < CURRENCY_COUNT; ++to)
        {
            if (from == to) { CHECK(!SmrExchangeQuote(from, to, 1, &debit, &credit)); continue; }
            ResetExchange();
            CHECK(SmrExchangeQuote(from, to, 1, &debit, &credit));
            CHECK((uint64_t)debit * kCurrencies[from].points == (uint64_t)credit * kCurrencies[to].points);
            unsigned before_from = saved_currency[from], before_to = saved_currency[to];
            CHECK(SmrExchange(&mod, from, to, 1));
            CHECK(!SmrExchange(&mod, from, to, 1) && wallet_writes == 0);
            CHECK(!BuyItem(0));
            SmrCurrency item;
            CHECK(SmrCurrencyItem(from, &item) && item.pending);
            Tick();
            CHECK(!exchange.pending && !shop_failed && wallet_writes == 1 && add_calls == 1);
            CHECK(saved_currency[from] == before_from - debit && saved_currency[to] == before_to + credit);
            CHECK(Amount(kCurrencies[from].id) == saved_currency[from] && Amount(kCurrencies[to].id) == saved_currency[to]);
            CHECK(!SmrCheckoutComplete());
            for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(!access[i]);
        }
    ResetExchange();
    CHECK(SmrExchangeQuote(3, 4, 1, &debit, &credit) && debit == 15 && credit == 4);
    CHECK(!SmrExchangeQuote(0, CURRENCY_COUNT, 1, &debit, &credit));
    CHECK(!SmrExchangeQuote(0, 1, 0, &debit, &credit));
    CHECK(!SmrExchangeQuote(0, 4, UINT_MAX, &debit, &credit));
    CHECK(!SmrExchange(&mod, 0, 4, 0));
    CHECK(!SmrExchange(&mod, 0, 4, 21));
    CHECK(!SmrExchange(&mod, 4, 3, 4)); /* 60 super would exceed 100. */
    CHECK(!SmrExchange(&mod, 4, 0, 54)); /* 40,500 Req would exceed 50,000. */
    CHECK(!SmrExchange(&mod, 4, 1, 27)); /* 405 common would exceed 500. */
    CHECK(!SmrExchange(&mod, 4, 2, 11)); /* 165 rare would exceed 250. */
    SetAmount(kCurrencies[3].id, 55);
    Tick();
    CHECK(SmrExchange(&mod, 4, 3, 3)); /* Exactly at the cap is allowed. */
    Tick();
    CHECK(Amount(kCurrencies[3].id) == 100);
    ResetExchange();
    SetAmount(REQUISITION, 101);
    Tick();
    CHECK(SmrExchange(&mod, 0, 1, 2));
    Tick();
    CHECK(Amount(REQUISITION) == 1 && saved_currency[0] == 1); /* Remainder retained. */
    ResetExchange();
    SetAmount(kCurrencies[4].id, 0);
    Tick();
    CHECK(SmrExchange(&mod, 0, 4, 1));
    Tick();
    CHECK(Amount(kCurrencies[4].id) == 1 && saved_currency[4] == 1);
    ResetExchange();
    SetAmount(kCurrencies[4].id, UINT_MAX);
    Tick();
    CHECK(!SmrExchange(&mod, 0, 4, 1)); /* Uncapped currencies still cannot overflow. */
    ResetExchange();
    CHECK(SmrExchange(&mod, 0, 4, 2));
    game_state = HF_STATE_MISSION;
    Tick();
    CHECK(!exchange.pending && !wallet_writes && !add_calls && Amount(REQUISITION) == 10000);
    CHECK(!SmrExchange(&mod, 0, 4, 1));
    ResetExchange();
    charge.state = CHARGE_QUEUED;
    CHECK(!SmrExchange(&mod, 0, 4, 1));
    charge.state = CHARGE_IDLE;
    CHECK(SmrExchange(&mod, 0, 4, 2));
    SetAmount(REQUISITION, 999);
    Tick();
    CHECK(!exchange.pending && wallet_writes == 1 && !add_calls && saved_wallet == 999);
    ResetExchange();
    CHECK(SmrExchange(&mod, 4, 3, 1));
    SetAmount(kCurrencies[3].id, 100);
    Tick();
    CHECK(!exchange.pending && wallet_writes == 1 && saved_currency[3] == 100 && Amount(kCurrencies[4].id) == 100);
    ResetExchange();
    unsigned ignored;
    uintptr_t capped = ItemRecord(&api, (uintptr_t)manager, kCurrencies[3].id, &ignored);
    CHECK(SmrExchange(&mod, 4, 3, 1));
    Put32((unsigned char*)capped + 0x18, 50);
    Tick();
    CHECK(!exchange.pending && !wallet_writes && Amount(kCurrencies[4].id) == 100);
    ResetExchange();
    uintptr_t record = ItemRecord(&api, (uintptr_t)manager, kCurrencies[4].id, &ignored);
    unreadable_at = record + 0x18;
    Tick();
    CHECK(!SmrExchange(&mod, 0, 4, 1));
    unreadable_at = 0;
    ResetExchange();
    for (int failure = 0; failure < 4; ++failure)
    {
        ResetExchange();
        remove_failed = failure == 0;
        add_failed = failure == 1 ? 1 : (failure == 3 ? 2 : 0);
        wallet_refused = failure == 2;
        CHECK(SmrExchange(&mod, 0, 4, 1));
        Tick();
        CHECK(shop_failed && !inventory_ready && !exchange_ready && !exchange.pending);
        CHECK(add_calls == (failure == 0 ? 0 : 1));
        CHECK(wallet_writes == (failure == 2 ? 1 : 0));
        CHECK(saved_currency[0] == 10000 && saved_currency[4] == 100);
        CHECK(!SmrExchange(&mod, 0, 4, 1));
        CHECK(!BuyItem(0));
    }
    ResetExchange();
    SetAmount(REQUISITION, 5000);
    Tick();
    wallet_writes = 0;
    puts("PASS exchanges: all 20 pairs, exact batches, remainders, caps, ship-only, stale balance/cap checks, shared transaction exclusion and partial failures");
}

static void TestDestroyer(void)
{
    ResetExchange();
    SetAmount(REQUISITION, 5000);
    Tick();
    wallet_writes = 0;
    const uint32_t credit_id = kCurrencies[FORGE_CREDITS].id, common_id = kCurrencies[COMMON_SAMPLES].id;
    const uint32_t rare_id = kCurrencies[RARE_SAMPLES].id, super_id = kCurrencies[SUPER_SAMPLES].id;
    const uint32_t picks[] = {HF_STRATAGEM_ORBITAL_PRECISION_STRIKE, HF_STRATAGEM_ORBITAL_WALKING_BARRAGE,
        HF_STRATAGEM_MS11_SOLO_SILO, HF_STRATAGEM_ORBITAL_PRECISION_STRIKE, HF_STRATAGEM_RESUPPLY,
        HF_STRATAGEM_DROPOFF_HELLBOMB};
    Put32(history + LOADOUT + STRATAGEM_COUNT, sizeof picks / sizeof picks[0]);
    for (size_t i = 0; i < sizeof picks / sizeof picks[0]; ++i)
        Put32(history + LOADOUT + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE, picks[i]);
    CHECK(!SmrSelectUpgrade(&mod, SMR_UPGRADE_ORBITAL, 0));
    OpenCheckout();
    session_role = HF_SESSION_HOST;
    CHECK(SmrDestroyer(&mod).available && SmrSelectUpgrade(&mod, SMR_UPGRADE_ORBITAL, 0));
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_NONE, 0));
    Tick();
    unsigned total, price[CURRENCY_COUNT];
    const unsigned orbital_price[CURRENCY_COUNT] = {2000, 10, 5, 1, 10};
    int pending;
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_SUPPORT, 0));
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && total == 2500 && price[FORGE_CREDITS] == 13);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_NONE, 0) && GearCheckout(&total, &pending) && total == 2500);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_ORBITAL, 0));
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && total == 2500 && !pending &&
        !memcmp(price, orbital_price, sizeof price));
    CHECK(!SmrDestroyer(&mod).purchased && Amount(REQUISITION) == 5000 && Amount(credit_id) == 100 && !wallet_writes);
    SmrFinishCheckout(0);
    CHECK(GearCheckout(&total, &pending));

    Pick(HF_ITEM_AR23_LIBERATOR, picks[0]);
    Tick();
    OpenCheckout();
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_ORBITAL, 0));
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && !total && price[FORGE_CREDITS] == 10 && !pending);
    CHECK(BuyCheckout() && !SmrExchange(&mod, 0, FORGE_CREDITS, 1));
    CHECK(!SmrSelectUpgrade(&mod, SMR_UPGRADE_EAGLE, 0) && !SmrSelectUpgrade(&mod, SMR_UPGRADE_NONE, 0));
    Tick();
    CHECK(SmrDestroyer(&mod).purchased == SMR_UPGRADE_ORBITAL && SmrCheckoutComplete() && !live_sets);
    CHECK(Amount(credit_id) == 90 && saved_currency[FORGE_CREDITS] == 90);
    CHECK(Amount(common_id) == 90 && Amount(rare_id) == 95 && Amount(super_id) == 49);
    CHECK(saved_currency[COMMON_SAMPLES] == 90 && saved_currency[RARE_SAMPLES] == 95 &&
        saved_currency[SUPER_SAMPLES] == 49 && SmrDestroyer(&mod).balance[SUPER_SAMPLES] == 49);
    CHECK(Amount(REQUISITION) == 3000 && saved_wallet == 3000 && wallet_writes == 1 &&
        SmrDestroyer(&mod).balance[REQUISITION_SLIPS] == 3000);
    SmrFinishCheckout(0);
    Put32(history + LOADOUT + STRATAGEM_COUNT, sizeof picks / sizeof picks[0]);
    for (size_t i = 0; i < sizeof picks / sizeof picks[0]; ++i)
        Put32(history + LOADOUT + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE, picks[i]);

    refused_type = picks[1];
    MissionStart(&mod, NULL, NULL);
    CHECK(!upgraded_count && orbital_cooldown == 100 && barrage_cooldown == 50 && support_cooldown == 80);
    MissionEnd(&mod, NULL, NULL);
    CHECK(upgrade_purchased == SMR_UPGRADE_ORBITAL);
    refused_type = 0;
    live_sets = 0;
    MissionStart(&mod, NULL, NULL);
    CHECK(upgraded_count == 3 && live_sets == 3);
    CHECK(orbital_cooldown == 34 && barrage_cooldown == 17 && support_cooldown == 120);
    CHECK(resupply_cooldown == 160);
    HfPlayerEvent spawn = {0};
    spawn.local = 1;
    PlayerSpawn(&mod, &spawn, NULL);
    CHECK(live_sets == 3 && Amount(credit_id) == 90);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!upgrade_purchased && !upgraded_count);
    CHECK(orbital_cooldown == 100 && barrage_cooldown == 50 && support_cooldown == 80);
    MissionStart(&mod, NULL, NULL);
    CHECK(live_sets == 3);
    MissionEnd(&mod, NULL, NULL);

    Tick();
    OpenCheckout();
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_SUPPORT, 0));
    SetAmount(REQUISITION, 4499);
    Tick();
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_BLOCKED && total == 2500 &&
        price[REQUISITION_SLIPS] == 2000 && !pending && !BuyCheckout());
    SetAmount(REQUISITION, 4500);
    Tick();
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && price[FORGE_CREDITS] == 13);
    upgrade_tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS] = UINT32_MAX;
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_BLOCKED && !BuyCheckout());
    upgrade_tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS] = 2000;

    CHECK(BuyCheckout());
    SetAmount(REQUISITION, 4499);
    Tick();
    CHECK(charge.state == CHARGE_IDLE && !SmrCheckoutComplete() && !upgrade_purchased &&
        Amount(REQUISITION) == 4499 && Amount(credit_id) == 90);
    SetAmount(REQUISITION, 4500);
    SetAmount(credit_id, 12);
    Tick();
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_BLOCKED && price[FORGE_CREDITS] == 13 && !BuyCheckout());
    SetAmount(credit_id, 90);
    SetAmount(super_id, 0);
    Tick();
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_BLOCKED && price[SUPER_SAMPLES] == 1 && !BuyCheckout());
    SetAmount(super_id, 49);
    Tick();
    unsigned requisition = Amount(REQUISITION);
    CHECK(BuyCheckout());
    SetAmount(rare_id, 4);
    Tick();
    CHECK(charge.state == CHARGE_IDLE && !shop_failed && !upgrade_purchased && !SmrCheckoutComplete() &&
        upgrade_selected == SMR_UPGRADE_SUPPORT);
    CHECK(Amount(REQUISITION) == requisition && Amount(credit_id) == 90 && Amount(common_id) == 90 &&
        Amount(rare_id) == 4 && Amount(super_id) == 49);
    SetAmount(rare_id, 95);
    Tick();
    CHECK(BuyCheckout());
    wallet_writes = 0;
    wallet_refused = 1;
    Tick();
    size_t silo = PriceIndex("MS-11 Solo Silo");
    CHECK(shop_failed && !upgrade_purchased && !access[silo] && !SmrCheckoutComplete());
    CHECK(wallet_writes == 1 && Amount(credit_id) == 77 && saved_currency[FORGE_CREDITS] == 90 &&
        Amount(REQUISITION) == saved_wallet - 4500);
    CHECK(Amount(common_id) == 80 && Amount(rare_id) == 90 && Amount(super_id) == 48 &&
        saved_currency[COMMON_SAMPLES] == 90 && saved_currency[RARE_SAMPLES] == 95 && saved_currency[SUPER_SAMPLES] == 49);
    ResetExchange();
    SetAmount(REQUISITION, 4000);
    Pick(HF_ITEM_AR23_LIBERATOR, picks[2]);
    Tick();
    OpenCheckout();
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_SUPPORT, 0) && BuyCheckout());
    wallet_writes = 0;
    Tick();
    CHECK(upgrade_purchased == SMR_UPGRADE_SUPPORT && access[silo] && SmrCheckoutComplete() && wallet_writes == 1);
    CHECK(Amount(credit_id) == 87 && saved_currency[FORGE_CREDITS] == 87 &&
        Amount(REQUISITION) == 0 && saved_wallet == 0);
    CHECK(Amount(common_id) == 90 && Amount(rare_id) == 95 && Amount(super_id) == 49 &&
        saved_currency[COMMON_SAMPLES] == 90 && saved_currency[RARE_SAMPLES] == 95 && saved_currency[SUPER_SAMPLES] == 49);
    const HfStateEvent prepare = {{0}, HF_STATE_SHIP, HF_STATE_PREPARE_MISSION};
    UpgradeStateChanged(&mod, &prepare, NULL);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!upgrade_purchased);
    SmrFinishCheckout(0);
    ResetExchange();
    session_role = HF_SESSION_SOLO;
    puts("PASS destroyer: free selection, combined Req costs, funds recheck, rollback, and one-mission cooldowns");
}

static void TestUpgradeEffects(void)
{
    const uint32_t picks[] = {HF_STRATAGEM_EAGLE_AIRSTRIKE, HF_STRATAGEM_EAGLE_500KG_BOMB,
        HF_STRATAGEM_AG16_GATLING_SENTRY, HF_STRATAGEM_MS11_SOLO_SILO, HF_STRATAGEM_ORBITAL_PRECISION_STRIKE};
    Put32(history + LOADOUT + STRATAGEM_COUNT, sizeof picks / sizeof picks[0]);
    for (size_t i = 0; i < sizeof picks / sizeof picks[0]; ++i)
        Put32(history + LOADOUT + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE, picks[i]);
    const HfStateEvent prepare = {{0}, HF_STATE_SHIP, HF_STATE_PREPARE_MISSION};
    const HfStateEvent abort = {{0}, HF_STATE_PREPARE_MISSION, HF_STATE_SHIP};

    upgrade_purchased = SMR_UPGRADE_DEFENSIVE;
    refused_type = HF_STRATAGEM_AG16_GATLING_SENTRY;
    refused_field = &kUses;
    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(!upgraded_count && sentry_cooldown == 90 && sentry_uses == 4 && eagle_cooldown == 8);
    CHECK(upgrade_purchased == SMR_UPGRADE_DEFENSIVE);
    refused_type = 0;
    refused_field = NULL;
    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(sentry_cooldown == 22.5 && sentry_uses == UINT32_MAX);
    UpgradeStateChanged(&mod, &abort, NULL);
    CHECK(!upgraded_count && sentry_cooldown == 90 && sentry_uses == 4 && rearm_cooldown == 120);
    CHECK(upgrade_purchased == SMR_UPGRADE_DEFENSIVE);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    CHECK(upgrade_purchased == SMR_UPGRADE_DEFENSIVE && !upgraded_count && sentry_uses == 4);

    for (SmrUpgrade choice = SMR_UPGRADE_ORBITAL; choice <= SMR_UPGRADE_SUPPORT; ++choice)
    {
        live_sets = 0;
        upgrade_purchased = choice;
        UpgradeStateChanged(&mod, &prepare, NULL);
        const int eagle = choice == SMR_UPGRADE_EAGLE;
        const int defensive = choice == SMR_UPGRADE_DEFENSIVE;
        const int support = choice == SMR_UPGRADE_SUPPORT;
        CHECK(rearm_cooldown == (eagle ? 30 : 180));
        CHECK(eagle_cooldown == (eagle ? 20 : 12) && eagle_call_in == (eagle ? 5 : 3));
        CHECK(second_eagle_cooldown == (eagle ? 20 : 15) && second_eagle_call_in == (eagle ? 6 : 4));
        CHECK(sentry_cooldown == (defensive ? 22.5 : 135) && sentry_uses == (defensive ? UINT32_MAX : 4));
        CHECK(support_cooldown == (support ? 40 : 120) && support_uses == (support ? UINT32_MAX : 3));
        CHECK(orbital_cooldown == (choice == SMR_UPGRADE_ORBITAL ? 34 : 150));
        int expected = 6 + (eagle ? 2 : defensive || support);
        MissionStart(&mod, NULL, NULL);
        CHECK(live_sets == expected && upgraded_count == (size_t)expected);
        MissionEnd(&mod, NULL, NULL);
        CHECK(!upgrade_purchased && !upgraded_count && rearm_cooldown == 120 && orbital_cooldown == 100);
        CHECK(eagle_cooldown == 8 && eagle_call_in == 3 && second_eagle_cooldown == 10 && second_eagle_call_in == 4);
        CHECK(sentry_cooldown == 90 && sentry_uses == 4 && support_cooldown == 80 && support_uses == 3);
    }
    upgrade_tuning[SMR_UPGRADE_EAGLE] = (SmrUpgradeTuning){.price = {[FORGE_CREDITS] = 7}, .other_cooldown_percent = -25,
        .cooldown_seconds = 5.5, .call_in_extra_seconds = 0.5, .rearm_seconds = 15};
    upgrade_purchased = SMR_UPGRADE_EAGLE;
    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(eagle_cooldown == 5.5 && eagle_call_in == 3.5 && rearm_cooldown == 15 && orbital_cooldown == 75);
    MissionEnd(&mod, NULL, NULL);
    puts("PASS destroyer effects: percentage cooldowns, unlimited uses, shared rearm, failed or aborted prep, and mission cleanup");
}

static unsigned char health_buckets[32], health_descriptor[24], health_record[0x1B8], health_ext[0x1C];
static void* health_descriptors[1];
static uint32_t injured_limbs;
static int heal_calls, heal_limb_calls;
static int Heal(HfMod* unused, float amount)
{
    heal_calls++;
    int32_t max;
    memcpy(&max, health_ext + 0x14, sizeof max);
    Put32(health_record + 0x14, (uint32_t)max);
    return HF_ACT_DONE;
}
static int HealLimb(HfMod* unused, uint32_t limb)
{
    heal_limb_calls++;
    injured_limbs &= ~(1u << limb);
    return HF_ACT_DONE;
}
static int LimbState(HfMod* unused, uint32_t entity, uint32_t limb, HfLimbState* out)
{
    *out = (HfLimbState){45, 45, (injured_limbs >> limb) & 1};
    return 1;
}
static double ItemCount(uint32_t id, uint8_t slot, int f)
{
    const HelldiverItem* item = HelldiverItemFor(id, slot);
    CHECK(item && f < item->count);
    return item->delta ? HelldiverStatNow(item, f) :
        HelldiverNow(item->resource, HelldiverComponentField(slot, item->fields[f].offset));
}
static int Near(double a, double b) { return a > b - 1e-6 && a < b + 1e-6; }
static uint32_t Weapon(int slot) { return *(uint32_t*)(history + LOADOUT + slot * WEAPON_STRIDE); }

static void TestHelldiver(void)
{
    const uint32_t avatar = 77;
    uintptr_t buckets = (uintptr_t)health_buckets, records = (uintptr_t)health_record, ext = (uintptr_t)health_ext;
    void* descriptors = health_descriptors;
    memset(health_buckets, 0xFF, sizeof health_buckets);
    Put32(health_buckets + (avatar % 4) * 8, avatar);
    Put32(health_buckets + (avatar % 4) * 8 + 4, 0);
    memcpy(health_manager + 0x1030, &buckets, sizeof buckets);
    Put32(health_manager + 0x1038, 4);
    Put32(health_manager + 0x103C, 0xFFFFFFFFu);
    Put32(health_manager + 0x1040, 1);
    Put32(health_manager + 0x1020, 1);
    health_descriptors[0] = health_descriptor;
    Put32(health_descriptor + 8, avatar);
    memcpy(health_manager + 0x1048, &descriptors, sizeof descriptors);
    memcpy(health_manager + 0x1058, &records, sizeof records);
    memcpy(health_manager + 0x1060, &ext, sizeof ext);
    Put32(health_ext + 0x14, 500);
    Put32(health_record + 0x14, 500);
    CHECK(HelldiverItemFor(HF_ITEM_AR23_LIBERATOR, 0) && HelldiverItemFor(HF_ITEM_P2_PEACEMAKER, 1) &&
        HelldiverItemFor(HF_ITEM_G12_HIGH_EXPLOSIVE, 2));

    ResetExchange();
    const uint32_t common_id = kCurrencies[COMMON_SAMPLES].id, rare_id = kCurrencies[RARE_SAMPLES].id,
        super_id = kCurrencies[SUPER_SAMPLES].id, credit_id = kCurrencies[FORGE_CREDITS].id;
    SetAmount(REQUISITION, 5000);
    SetAmount(common_id, 100);
    SetAmount(rare_id, 100);
    SetAmount(super_id, 50);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    Tick();
    unsigned credits = Amount(credit_id), total, price[CURRENCY_COUNT];
    int pending;
    CHECK(!SmrSelectHelldiver(&mod, SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF));
    OpenCheckout();
    session_role = HF_SESSION_HOST;
    CHECK(SmrHelldiver(&mod).available[SMR_TIER_PLUS] &&
        SmrSelectHelldiver(&mod, SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF));
    CHECK(!SmrSelectHelldiver(&mod, SMR_TIER_PLUS, SMR_PERK_ARMORED));
    CHECK(SmrHelldiver(&mod).available[SMR_TIER_SUPER]);
    CHECK(SmrSelectHelldiver(&mod, SMR_TIER_PLUS, SMR_PERK_LIGHT_KIT) &&
        SmrSelectHelldiver(&mod, SMR_TIER_ELITE, SMR_PERK_ARMORED) &&
        SmrSelectHelldiver(&mod, SMR_TIER_SUPER, SMR_PERK_AUTO_STIM));
    CHECK(SmrSelectHelldiver(&mod, SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF));
    SmrHelldiverStatus status = SmrHelldiver(&mod);
    CHECK(status.selected[SMR_TIER_PLUS] == SMR_PERK_PRIMARY_TRADEOFF && status.selected[SMR_TIER_ELITE] == SMR_PERK_ARMORED &&
        status.selected[SMR_TIER_SUPER] == SMR_PERK_AUTO_STIM);
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && !total && !pending &&
        price[COMMON_SAMPLES] == 50 && price[RARE_SAMPLES] == 25 && price[SUPER_SAMPLES] == 5 &&
        !price[REQUISITION_SLIPS] && !price[FORGE_CREDITS]);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_ORBITAL, 0));
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && price[REQUISITION_SLIPS] == 2000 &&
        price[COMMON_SAMPLES] == 60 && price[RARE_SAMPLES] == 30 && price[SUPER_SAMPLES] == 6 && price[FORGE_CREDITS] == 10);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_NONE, 0));
    SetAmount(super_id, 4);
    Tick();
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_BLOCKED && !BuyCheckout());
    SetAmount(super_id, 50);
    Tick();
    CHECK(BuyCheckout());
    Tick();
    status = SmrHelldiver(&mod);
    CHECK(SmrCheckoutComplete() && status.purchased[SMR_TIER_PLUS] == SMR_PERK_PRIMARY_TRADEOFF &&
        status.purchased[SMR_TIER_ELITE] == SMR_PERK_ARMORED && status.purchased[SMR_TIER_SUPER] == SMR_PERK_AUTO_STIM &&
        !status.selected[SMR_TIER_PLUS] && !status.selected[SMR_TIER_ELITE] && !status.selected[SMR_TIER_SUPER]);
    CHECK(Amount(common_id) == 50 && Amount(rare_id) == 75 && Amount(super_id) == 45 && Amount(credit_id) == credits &&
        Amount(REQUISITION) == 5000);
    CHECK(saved_currency[COMMON_SAMPLES] == 50 && saved_currency[RARE_SAMPLES] == 75 && saved_currency[SUPER_SAMPLES] == 45);
    SmrFinishCheckout(0);
    OpenCheckout();
    status = SmrHelldiver(&mod);
    CHECK(!status.available[SMR_TIER_PLUS] && !SmrSelectHelldiver(&mod, SMR_TIER_PLUS, SMR_PERK_LIGHT_KIT));
    SmrFinishCheckout(0);

    const HfStateEvent prepare = {{0}, HF_STATE_SHIP, HF_STATE_PREPARE_MISSION};
    const HfStateEvent abort = {{0}, HF_STATE_PREPARE_MISSION, HF_STATE_SHIP};
    helldiver_stat_sets_before_refusal = 1;
    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(!helldiver_applied && !helldiver_edit_count && Weapon(0) == HF_ITEM_AR23_LIBERATOR &&
        Weapon(1) == HF_ITEM_P2_PEACEMAKER && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 6 &&
        ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 6);
    helldiver_stat_sets_before_refusal = -1;
    UpgradeStateChanged(&mod, &abort, NULL);
    CHECK(helldiver_purchased[SMR_TIER_SUPER] == SMR_PERK_AUTO_STIM);

    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(helldiver_applied && Weapon(0) == HF_ITEM_AR23_LIBERATOR && Weapon(1) == HF_ITEM_P2_PEACEMAKER);
    CHECK(ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 8 && ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 1) == 8 &&
        ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 3 && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 1) == 3);
    CHECK(Near(HelldiverNow(HELLDIVER_RESOURCE, &kSprint), 10));
    UpgradeStateChanged(&mod, &abort, NULL);
    CHECK(!helldiver_applied && Weapon(0) == HF_ITEM_AR23_LIBERATOR && Weapon(1) == HF_ITEM_P2_PEACEMAKER &&
        ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 6 && ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 6);

    UpgradeStateChanged(&mod, &prepare, NULL);
    MissionStart(&mod, NULL, NULL);
    CHECK(helldiver_applied && !waiting_for_spawn && Weapon(0) == HF_ITEM_AR23_LIBERATOR &&
        Weapon(1) == HF_ITEM_P2_PEACEMAKER);
    game_state = HF_STATE_MISSION;
    HfWeaponEvent weapon = {0};
    weapon.avatar = avatar;
    HelldiverWeapon(&mod, &weapon, NULL);
    HfDamagePreEvent hit = {0};
    hit.damage.target = avatar;
    hit.damage.amount = 100;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(Near(hit.damage.amount, 89) && !hit.cancel);
    hit.damage.amount = -1;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(hit.damage.amount == -1);
    hit.damage.target = avatar + 1;
    hit.damage.amount = 100;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(hit.damage.amount == 100);
    Put32(health_record + 0x14, 120);
    HelldiverTick(&mod, 0.1f);
    CHECK(!heal_calls);
    Put32(health_record + 0x14, 90);
    HelldiverTick(&mod, 0.1f);
    CHECK(heal_calls == 1 && *(int32_t*)(health_record + 0x14) == 500);
    Put32(health_record + 0x14, 90);
    HelldiverTick(&mod, 10);
    CHECK(heal_calls == 1);
    Put32(health_record + 0x19C, 1);
    HelldiverTick(&mod, 25);
    CHECK(heal_calls == 1);
    Put32(health_record + 0x19C, 0);
    HelldiverTick(&mod, 0.1f);
    CHECK(heal_calls == 2);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!helldiver_applied && !helldiver_purchased[SMR_TIER_PLUS] && !helldiver_purchased[SMR_TIER_ELITE] &&
        !helldiver_purchased[SMR_TIER_SUPER] && Weapon(0) == HF_ITEM_AR23_LIBERATOR &&
        Weapon(1) == HF_ITEM_P2_PEACEMAKER && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 6 &&
        ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 6);
    hit.damage.target = avatar;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(hit.damage.amount == 100);

    game_state = HF_STATE_SHIP;
    helldiver_purchased[SMR_TIER_PLUS] = SMR_PERK_SECONDARY_TRADEOFF;
    MissionStart(&mod, NULL, NULL);
    CHECK(helldiver_applied && Weapon(0) == HF_ITEM_AR23_LIBERATOR && Weapon(1) == HF_ITEM_P2_PEACEMAKER);
    CHECK(ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 9 && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 1) == 9 &&
        ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 3 && ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 1) == 3);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!helldiver_purchased[SMR_TIER_PLUS] && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 6 &&
        ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 6);

    helldiver_purchased[SMR_TIER_PLUS] = SMR_PERK_LIGHT_KIT;
    helldiver_purchased[SMR_TIER_ELITE] = SMR_PERK_ARMORED;
    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(helldiver_applied && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 7 &&
        ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 7);
    MissionStart(&mod, NULL, NULL);
    game_state = HF_STATE_MISSION;
    HelldiverWeapon(&mod, &weapon, NULL);
    hit.damage.amount = 100;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(hit.damage.amount == 89);
    MissionEnd(&mod, NULL, NULL);
    game_state = HF_STATE_SHIP;
    CHECK(!helldiver_applied && !helldiver_purchased[SMR_TIER_PLUS] &&
        ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 6 && ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 6);

    helldiver_purchased[SMR_TIER_PLUS] = SMR_PERK_LIGHT_KIT;
    helldiver_purchased[SMR_TIER_ELITE] = SMR_PERK_SPRINTER;
    helldiver_purchased[SMR_TIER_SUPER] = SMR_PERK_FIELD_MEDIC;
    UpgradeStateChanged(&mod, &prepare, NULL);
    CHECK(helldiver_applied && Weapon(0) == HF_ITEM_AR23_LIBERATOR && Weapon(1) == HF_ITEM_P2_PEACEMAKER);
    CHECK(ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 0) == 8 && ItemCount(HF_ITEM_AR23_LIBERATOR, 0, 1) == 8 &&
        ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 0) == 8 && ItemCount(HF_ITEM_P2_PEACEMAKER, 1, 1) == 8 &&
        ItemCount(HF_ITEM_G12_HIGH_EXPLOSIVE, 2, 0) == 2 &&
        ItemCount(HF_ITEM_G12_HIGH_EXPLOSIVE, 2, 1) == 2 && ItemCount(HF_ITEM_G12_HIGH_EXPLOSIVE, 2, 2) == 2);
    CHECK(Near(HelldiverNow(HELLDIVER_RESOURCE, &kSprint), 9.9) &&
        Near(HelldiverNow(HELLDIVER_RESOURCE, &kStamina), 7.5) && Near(HelldiverNow(HELLDIVER_RESOURCE, &kMovement[0]), 9));
    MissionStart(&mod, NULL, NULL);
    game_state = HF_STATE_MISSION;
    HelldiverWeapon(&mod, &weapon, NULL);
    hit.damage.target = avatar;
    hit.damage.amount = 100;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(hit.damage.amount == 123);
    injured_limbs = 1u << HF_LIMB_LEFT_LEG;
    HelldiverTick(&mod, 1);
    CHECK(!heal_limb_calls && injured_limbs);
    HelldiverTick(&mod, 1);
    CHECK(heal_limb_calls == 1 && !injured_limbs && heal_calls == 2);
    MissionEnd(&mod, NULL, NULL);
    game_state = HF_STATE_SHIP;
    CHECK(!helldiver_purchased[SMR_TIER_SUPER] && ItemCount(HF_ITEM_G12_HIGH_EXPLOSIVE, 2, 0) == 4 &&
        Near(HelldiverNow(HELLDIVER_RESOURCE, &kSprint), 10) && Near(HelldiverNow(HELLDIVER_RESOURCE, &kMovement[0]), 10));
    hit.damage.amount = 100;
    HelldiverDamagePre(&mod, &hit, NULL);
    CHECK(Near(hit.damage.amount, 100));
    ResetExchange();
    session_role = HF_SESSION_SOLO;
    puts("PASS helldiver: one stacked perk per tier, combined sample prices, mission apply, ammo trade-offs, "
        "damage scaling, auto-stim, field medic and one-mission use");
}

static bool SlotOwned(uintptr_t inventory, uint32_t type)
{
    CHECK(inventory == (uintptr_t)manager);
    return type != HF_STRATAGEM_AG16_GATLING_SENTRY;
}

static uint32_t SlotPackages(uint64_t* out, uint32_t type, uint32_t unused, uint32_t level)
{
    if (type == HF_STRATAGEM_EAGLE_500KG_BOMB)
    {
        CHECK(!unused && !level);
        out[0] = 103;
        return 1;
    }
    CHECK(type == HF_STRATAGEM_MS11_SOLO_SILO && unused == 0 && (level == 0 || level == 2));
    out[0] = 101;
    if (level == 2) out[1] = 102;
    return level == 2 ? 2 : 1;
}

static int LoadPackage(HfMod* unused, uint64_t package)
{
    CHECK(package == 101 || package == 102 || package == 103);
    return package == 101 && package_state ? HF_PACKAGE_RESIDENT : package_state;
}

static void SlotGrant(uintptr_t owner, uint32_t player, const ExtraGrantEntry* entry, uint32_t count)
{
    CHECK(owner == (uintptr_t)history && player == 193 && count >= 1 && count <= 2);
    grant_calls++;
    if (refuse_grant) return;
    uint32_t slots;
    memcpy(&slots, history + LOADOUT + STRATAGEM_COUNT, sizeof slots);
    CHECK(slots + count <= EXTRA_MISSION_LIMIT);
    for (uint32_t i = 0; i < count; ++i)
    {
        HfStratagemSettings row;
        CHECK(entry[i].mission_grant == 1 && !entry[i].override_uses && !entry[i].uses &&
            ExtraRow(&api, entry[i].type, &row));
        unsigned char* at = history + LOADOUT + STRATAGEM_SLOTS + STRATAGEM_STRIDE * (slots + i);
        memcpy(at, &entry[i], sizeof entry[i]);
        Put32(at + 4, row.uses);
    }
    Put32(history + LOADOUT + STRATAGEM_COUNT, slots + count);
}

static void TestExtraEagle(void)
{
    const uint32_t eagle = HF_STRATAGEM_EAGLE_500KG_BOMB, credit_id = kCurrencies[FORGE_CREDITS].id;
    const size_t bomb = PriceIndex("Eagle 500kg Bomb");
    ResetExchange();
    Pick(HF_ITEM_AR23_LIBERATOR, HF_STRATAGEM_ORBITAL_PRECISION_STRIKE);
    RefreshExtraOptions(&mod);
    OpenCheckout();
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, eagle));
    Tick();
    CHECK(BuyCheckout());
    Tick();
    CHECK(SmrCheckoutComplete() && upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT && access[bomb] &&
        Amount(REQUISITION) == 7500 && Amount(credit_id) == 50);
    SmrFinishCheckout(0);

    game_state = HF_STATE_MISSION;
    static const struct { uint32_t count, has_rearm, added; } cases[] = {
        {15, 0, 0}, {14, 0, 2}, {15, 1, 1}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const uint32_t count = cases[i].count;
        ExtraGrantEntry initial[EXTRA_MISSION_LIMIT] = {{.type = HF_STRATAGEM_ORBITAL_PRECISION_STRIKE}};
        for (uint32_t j = 1; j < count; ++j)
            initial[j] = (ExtraGrantEntry){.type = HF_STRATAGEM_RESUPPLY, .mission_grant = 1};
        if (cases[i].has_rearm)
        {
            initial[1] = (ExtraGrantEntry){.type = HF_STRATAGEM_EAGLE_AIRSTRIKE, .uses = 2};
            initial[2] = (ExtraGrantEntry){.type = HF_STRATAGEM_EAGLE_REARM, .uses = UINT32_MAX, .mission_grant = 1};
            upgrade_purchased = SMR_UPGRADE_EXTRA_SLOT;
            extra_pick = eagle;
            access[bomb] = 1;
        }
        unsigned char* slots = history + LOADOUT + STRATAGEM_SLOTS;
        memcpy(slots, initial, sizeof initial);
        Put32(history + LOADOUT + STRATAGEM_COUNT, count);
        const int calls = grant_calls;
        MissionStart(&mod, NULL, NULL);
        for (int tick = 0; tick < 4; ++tick) ExtraSlotTick(&mod, 1);
        CHECK(!memcmp(initial, slots, count * sizeof initial[0]));
        uint32_t picks[MAX_STRATAGEMS], after;
        CHECK(ReadStratagems(&api, (uintptr_t)history + LOADOUT, picks, &after) && after == count + cases[i].added);
        if (cases[i].added)
        {
            CHECK(grant_calls == calls + 1 && deployed_access[bomb] && picks[after - 1] == eagle);
            if (!cases[i].has_rearm) CHECK(picks[count] == HF_STRATAGEM_EAGLE_REARM);
            ExtraGrantEntry granted;
            memcpy(&granted, slots + (after - 1) * sizeof granted, sizeof granted);
            CHECK(granted.uses == 2 && granted.mission_grant == 1);
            CHECK(second_eagle_cooldown == 11 && rearm_cooldown == 132 && upgraded_count == 3 + cases[i].has_rearm);
        }
        else CHECK(grant_calls == calls && !upgraded_count && !deployed_access[bomb]);
        MissionEnd(&mod, NULL, NULL);
        CHECK(second_eagle_cooldown == 10 && rearm_cooldown == 120 && !upgraded_count);
        CHECK(cases[i].added ? !upgrade_purchased && !extra_pick && !access[bomb] :
            upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT && extra_pick == eagle && access[bomb]);
    }
    puts("PASS extra Eagles: checkout, two-entry capacity, shared Rearm reuse, stock charges, and cooldown cleanup");
}

static void TestExtraSlot(void)
{
    ResetExchange();
    api.load_package = LoadPackage;
    const uint32_t type = HF_STRATAGEM_MS11_SOLO_SILO, credit_id = kCurrencies[FORGE_CREDITS].id;
    const size_t silo = PriceIndex("MS-11 Solo Silo");
    unsigned char players[0x430] = {0}, mission[0x18] = {0}, descriptor[12] = {0};
    unsigned char* hud = calloc(EXTRA_HUD_READY + 1, 1);
    CHECK(hud);
    HfGameMode mode = {.mode = 1, .mission = mission};
    uint32_t map[] = {193, 1};
    Put32(mission + 0x14, 1);
    Put32(players + 0x84, 2);
    Put32(players + 0x88, 1);
    Put32(players + 0x2E0, 2);
    Put32(players + 0x3A8, 13);
    uint64_t other_peer = local.peer + 1;
    memcpy(players + 0x2C8, &other_peer, sizeof other_peer);
    memcpy(players + 0x2C8 + 0x38, &local.peer, sizeof local.peer);
    Put32(players + 0x2E0 + 0x38, 3);
    Put32(players + 0x3A8 + 0x20, 14);
    Put32(players + 0xD8, 1);
    Put32(descriptor + 8, 193);
    void* ptr = map;
    memcpy(players + 0xD0, &ptr, sizeof ptr);
    ptr = descriptor;
    memcpy(players + 0xE8, &ptr, sizeof ptr);
    memcpy(players + 0xE8 + 8, &ptr, sizeof ptr);
    slot_players = (uintptr_t)players;
    slot_mode = (uintptr_t)&mode;
    PutPointer(EXTRA_HUD, hud);
    PutPointer(EXTRA_OWNER, history);
    Put32(image + EXTRA_TABLE_LEA + 3, (uint32_t)(0x5000 - (EXTRA_TABLE_LEA + 7)));
    HfStratagemSettings rows[] = {
        {.type = type, .stable_id = 1, .code_length = 6, .uses = 3, .delivery = 2, .package = 101, .hud_type = HUD_SUPPORT},
        {.type = HF_STRATAGEM_ORBITAL_PRECISION_STRIKE, .stable_id = 2, .code_length = 3, .uses = UINT32_MAX, .delivery = 1},
        {.type = HF_STRATAGEM_EAGLE_AIRSTRIKE, .stable_id = 3, .code_length = 4, .uses = 2, .delivery = DELIVERY_EAGLE},
        {.type = HF_STRATAGEM_AG16_GATLING_SENTRY, .stable_id = 4, .code_length = 5, .uses = UINT32_MAX,
            .delivery = 2, .hud_type = HUD_DEFENSIVE},
        {.type = HF_STRATAGEM_EAGLE_500KG_BOMB, .stable_id = 5, .code_length = 5, .uses = 2, .delivery = DELIVERY_EAGLE,
            .package = 103},
        {.type = HF_STRATAGEM_EAGLE_REARM, .stable_id = 6, .code_length = 4, .uses = UINT32_MAX, .delivery = DELIVERY_REARM},
    };
    uint64_t variant[] = {2, 0, 0, 102};
    ptr = variant;
    memcpy((unsigned char*)&rows[0] + 0x118, &ptr, sizeof ptr);
    Put32((unsigned char*)&rows[0] + 0x120, 1);
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i)
        PutPointer(0x5000 + rows[i].type * sizeof(uintptr_t), rows + i);
    extra_native.available = SlotOwned;
    extra_native.packages = SlotPackages;
    extra_native.grant = SlotGrant;
    Pick(HF_ITEM_AR23_LIBERATOR, HF_STRATAGEM_ORBITAL_PRECISION_STRIKE);
    RefreshExtraOptions(&mod);
    SmrExtraChoice choices[4];
    CHECK(SmrExtraChoices(choices, 4) == 3);
    CHECK(choices[0].type == HF_STRATAGEM_EAGLE_500KG_BOMB && choices[0].category == HF_CATEGORY_OFFENSIVE);
    CHECK(choices[1].type == HF_STRATAGEM_EAGLE_AIRSTRIKE && choices[1].category == HF_CATEGORY_OFFENSIVE);
    CHECK(choices[2].type == type && choices[2].category == HF_CATEGORY_SUPPLY && !strcmp(choices[2].name, "MS-11 Solo Silo"));
    OpenCheckout();
    CHECK(!SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, 0));
    CHECK(!SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, HF_STRATAGEM_EAGLE_REARM));
    session_role = HF_SESSION_HOST;
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, type));
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_NONE, 0));
    unsigned total, price[CURRENCY_COUNT];
    int pending;
    const size_t bomb = PriceIndex("Eagle 500kg Bomb");
    SmrItem item;
    /* Changing extras updates both the price and itemized charges, even before the next tick. */
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, type));
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && total == 2000);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, HF_STRATAGEM_EAGLE_500KG_BOMB));
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && total == kLoadoutPrices[bomb].requisition);
    CHECK(SmrShopItem(bomb, &item) && item.selected && SmrShopItem(silo, &item) && !item.selected);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_NONE, 0) && !GearCheckout(&total, &pending) && !total);
    CHECK(SmrSelectUpgrade(&mod, SMR_UPGRADE_EXTRA_SLOT, type) && !wallet_writes);
    /* Prepaid native access leaves only the upgrade and restricted extra access due. */
    const uint32_t native_picks[] = {HF_STRATAGEM_ORBITAL_PRECISION_STRIKE, HF_STRATAGEM_EAGLE_AIRSTRIKE,
        HF_STRATAGEM_EAGLE_500KG_BOMB, HF_STRATAGEM_AG16_GATLING_SENTRY};
    Put32(history + LOADOUT + STRATAGEM_COUNT, 4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        unsigned char* at = history + LOADOUT + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE;
        memset(at, 0, STRATAGEM_STRIDE);
        Put32(at, native_picks[i]);
    }
    access[PriceIndex("Eagle 500kg Bomb")] = 1;
    access[PriceIndex("A/G-16 Gatling Sentry")] = 1;
    Tick();
    CHECK(SmrCheckout(&total, price, &pending) == SMR_CHECKOUT_PURCHASE && total == 2000 &&
        price[REQUISITION_SLIPS] == 2000 && price[FORGE_CREDITS] == 50 && !pending);
    CHECK(BuyCheckout());
    Tick();
    CHECK(!upgrade_purchased && !access[silo] && Amount(REQUISITION) == 10000 && Amount(credit_id) == 100);
    package_state = HF_PACKAGE_LOADING;
    CHECK(BuyCheckout());
    Tick();
    CHECK(SmrCheckoutComplete() && upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT && access[silo] &&
        Amount(REQUISITION) == 6000 && Amount(credit_id) == 50);
    SmrFinishCheckout(0);
    CHECK(SmrDestroyer(&mod).extra == type);
    unsigned char saved_loadout[0x200];
    memcpy(saved_loadout, profile, sizeof saved_loadout);

    game_state = HF_STATE_MISSION;
    unsigned char* objective = history + LOADOUT + STRATAGEM_SLOTS + 4 * STRATAGEM_STRIDE;
    memset(objective, 0, STRATAGEM_STRIDE);
    Put32(objective, HF_STRATAGEM_ORBITAL_WALKING_BARRAGE);
    objective[offsetof(ExtraGrantEntry, mission_grant)] = 1;
    Put32(history + LOADOUT + STRATAGEM_COUNT, 5);
    MissionStart(&mod, NULL, NULL);
    ExtraSlotTick(&mod, 1);
    CHECK(!grant_calls && !upgraded_count);
    hud[EXTRA_HUD_READY] = 1;
    ExtraSlotTick(&mod, 1);
    ExtraSlotTick(&mod, 1);
    CHECK(!grant_calls && !upgraded_count); /* Only the variant package is still loading. */
    package_state = HF_PACKAGE_RESIDENT;
    session_role = HF_SESSION_JOINER;
    Put32(mission + 0x14, 0);
    ExtraSlotTick(&mod, 1);
    CHECK(grant_calls == 1 && deployed_access[silo] && upgraded_count == 6);
    ExtraGrantEntry deployed;
    memcpy(&deployed, objective + STRATAGEM_STRIDE, sizeof deployed);
    CHECK(deployed.type == type && deployed.uses == 3 && deployed.mission_grant);
    CHECK((float)orbital_cooldown == 110 && (float)support_cooldown == 88 && (float)rearm_cooldown == 132 &&
        barrage_cooldown == 50);
    ExtraSlotTick(&mod, 1);
    CHECK(grant_calls == 1 && !memcmp(profile, saved_loadout, sizeof saved_loadout));
    MissionEnd(&mod, NULL, NULL);
    CHECK(!upgraded_count && !extra_pick && !upgrade_purchased && !access[silo] && orbital_cooldown == 100 &&
        support_cooldown == 80 && rearm_cooldown == 120);

    upgrade_purchased = SMR_UPGRADE_EXTRA_SLOT;
    extra_pick = type;
    Put32(history + LOADOUT + STRATAGEM_COUNT, EXTRA_MISSION_LIMIT);
    ExtraSlotTick(&mod, 1);
    ExtraSlotTick(&mod, 1);
    MissionEnd(&mod, NULL, NULL);
    CHECK(grant_calls == 1 && extra_pick == type && upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT);
    Put32(history + LOADOUT + STRATAGEM_COUNT, 1);
    refuse_grant = 1;
    ExtraSlotTick(&mod, 1);
    ExtraSlotTick(&mod, 1);
    ExtraSlotTick(&mod, 1);
    MissionEnd(&mod, NULL, NULL);
    CHECK(grant_calls == 2 && upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT && !upgraded_count &&
        orbital_cooldown == 100 && support_cooldown == 80);
    upgrade_purchased = SMR_UPGRADE_NONE;
    extra_pick = 0;
    refuse_grant = 0;
    TestExtraEagle();
    memset(&extra_native, 0, sizeof extra_native);
    api.load_package = NULL;
    ResetExchange();
    free(hud);
    puts("PASS extra slot: eligible picks and checkout, asset gate, single append, native limits, and mission cleanup");
}

static void TestExpandedCatalog(void)
{
    static const struct { uint32_t id; PriceKind kind; unsigned slot, price; const char* name; } expected[] = {
        {HF_ITEM_AR23P_LIBERATOR_PENETRATOR, PRICE_WEAPON, 0, 500, "AR-23P Liberator Penetrator"},
        {HF_ITEM_GP20_ULTIMATUM, PRICE_WEAPON, WEAPON_STRIDE, 500, "GP-20 Ultimatum"},
        {HF_ITEM_P40K_BOLT_PISTOL, PRICE_WEAPON, WEAPON_STRIDE, 500, "P/40-K Bolt Pistol"},
        {HF_ITEM_G40K_MELTA_MINE, PRICE_WEAPON, 2 * WEAPON_STRIDE, 500, "G/40-K Melta Mine"},
        {HF_ITEM_G48_GIGA_GRENADE, PRICE_WEAPON, 2 * WEAPON_STRIDE, 500, "G-48 Giga Grenade"},
        {HF_STRATAGEM_MS11_SOLO_SILO, PRICE_STRATAGEM, STRATAGEM_SLOTS, 2000, "MS-11 Solo Silo"},
        {HF_STRATAGEM_EAGLE_500KG_BOMB, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Eagle 500kg Bomb"},
        {HF_STRATAGEM_RS422_RAILGUN, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "RS-422 Railgun"},
        {HF_STRATAGEM_ORBITAL_WALKING_BARRAGE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Orbital Walking Barrage"},
        {HF_STRATAGEM_ORBITAL_LASER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Orbital Laser"},
        {HF_STRATAGEM_ARC3_ARC_THROWER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "ARC-3 Arc Thrower"},
        {HF_ITEM_PLAS39_ACCELERATOR_RIFLE, PRICE_WEAPON, 0, 500, "PLAS-39 Accelerator Rifle"},
        {HF_ITEM_R6_DEADEYE, PRICE_WEAPON, 0, 500, "R-6 Deadeye"},
        {HF_ITEM_R63CS_DILIGENCE_COUNTER_SNIPER, PRICE_WEAPON, 0, 500, "R-63CS Diligence Counter Sniper"},
        {HF_ITEM_R72_CENSOR, PRICE_WEAPON, 0, 500, "R-72 Censor"},
        {HF_ITEM_R40K_HOTSHOT_MARKSMAN_RIFLE, PRICE_WEAPON, 0, 500, "R/40-K Hot-Shot Marksman Rifle"},
        {HF_ITEM_LAS5_SCYTHE, PRICE_WEAPON, 0, 500, "LAS-5 Scythe"},
        {HF_ITEM_AR59_SUPPRESSOR, PRICE_WEAPON, 0, 500, "AR-59 Suppressor"},
        {HF_ITEM_LAS17_DOUBLEEDGE_SICKLE, PRICE_WEAPON, 0, 500, "LAS-17 Double-Edge Sickle"},
        {HF_ITEM_P4_SENATOR, PRICE_WEAPON, WEAPON_STRIDE, 500, "P-4 Senator"},
        {HF_STRATAGEM_LAS98_LASER_CANNON, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "LAS-98 Laser Cannon"},
        {HF_STRATAGEM_LAS99_QUASAR_CANNON, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "LAS-99 Quasar Cannon"},
        {HF_STRATAGEM_M1000_MAXIGUN, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "M-1000 Maxigun"},
        {HF_STRATAGEM_PLAS45_EPOCH, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "PLAS-45 Epoch"},
        {HF_STRATAGEM_STAX3_W_A_S_P_LAUNCHER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "StA-X3 W.A.S.P. Launcher"},
        {HF_STRATAGEM_GL28_BELTFED_GRENADE_LAUNCHER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "GL-28 Belt-Fed Grenade Launcher"},
        {HF_STRATAGEM_RL77_AIRBURST_ROCKET_LAUNCHER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "RL-77 Airburst Rocket Launcher"},
        {HF_STRATAGEM_LIFT850_JUMP_PACK, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "LIFT-850 Jump Pack"},
        {HF_STRATAGEM_LIFT860_HOVER_PACK, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "LIFT-860 Hover Pack"},
        {HF_STRATAGEM_SH32_SHIELD_GENERATOR_PACK, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "SH-32 Shield Generator Pack"},
        {HF_STRATAGEM_EAT411_LEVELLER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "EAT-411 Leveller"},
        {HF_STRATAGEM_EXO45_PATRIOT, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "EXO-45 Patriot Exosuit"},
        {HF_STRATAGEM_EXO49_EMANCIPATOR_EXOSUIT, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "EXO-49 Emancipator Exosuit"},
        {HF_STRATAGEM_EXO51_LUMBERER_EXOSUIT, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "EXO-51 Lumberer Exosuit"},
        {HF_STRATAGEM_EXO55_BREAKTHROUGH_EXOSUIT, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "EXO-55 Breakthrough Exosuit"},
        {HF_STRATAGEM_ORBITAL_380MM_BARRAGE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Orbital 380mm HE Barrage"},
        {HF_STRATAGEM_ORBITAL_120MM_BARRAGE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Orbital 120mm HE Barrage"},
        {HF_STRATAGEM_ORBITAL_NAPALM_BARRAGE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Orbital Napalm Barrage"},
        {HF_STRATAGEM_ORBITAL_RAILCANNON_STRIKE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "Orbital Railcannon Strike"},
        {HF_STRATAGEM_AC8_AUTOCANNON, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "AC-8 Autocannon"},
        {HF_STRATAGEM_AG16_GATLING_SENTRY, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "A/G-16 Gatling Sentry"},
        {HF_STRATAGEM_APW1_ANTIMATERIEL_RIFLE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "APW-1 Anti-Materiel Rifle"},
        {HF_STRATAGEM_AXAR23_GUARD_DOG, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "AX/AR-23 Guard Dog / gun"},
        {HF_STRATAGEM_GR8_RECOILLESS_RIFLE, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "GR-8 Recoilless Rifle"},
        {HF_STRATAGEM_MLS4X_COMMANDO, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "MLS-4X Commando"},
        {HF_STRATAGEM_B1_SUPPLY_PACK, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "B-1 Supply Pack"},
        {HF_ITEM_R4_HYENA, PRICE_WEAPON, 0, 500, "R-4 Hyena"},
        {HF_STRATAGEM_AMLS4X_ROCKET_SENTRY, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "A/MLS-4X Rocket Sentry"},
        {HF_STRATAGEM_AAC8_AUTOCANNON_SENTRY, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "A/AC-8 Autocannon Sentry"},
        {HF_STRATAGEM_AM12_MORTAR_SENTRY, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "A/M-12 Mortar Sentry"},
        {HF_STRATAGEM_EAT12_ANTITANK_EMPLACEMENT, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "E/AT-12 Anti-Tank Emplacement"},
        {HF_STRATAGEM_B100_PORTABLE_HELLBOMB, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "B-100 Portable Hellbomb"},
        {HF_STRATAGEM_BMD_C4_PACK, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "B/MD C4 Pack"},
        {HF_STRATAGEM_M103_SUPPLY_FRV, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "M-103 Supply FRV"},
        {HF_STRATAGEM_TD220_BASTION_MK_XVI, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "TD-220 Bastion MK XVI"},
        {HF_STRATAGEM_TD110_MAELSTROM, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "TD-110 Maelstrom"},
        {HF_STRATAGEM_FAF14_SPEAR, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "FAF-14 Spear"},
        {HF_STRATAGEM_MG206_HEAVY_MACHINE_GUN, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "MG-206 Heavy Machine Gun"},
        {HF_STRATAGEM_40K_MELTAGUN, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "40-K Meltagun"},
        {HF_STRATAGEM_GL21_GRENADE_LAUNCHER, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "GL-21 Grenade Launcher"}
    };
    CHECK(PRICE_COUNT == 2 + sizeof expected / sizeof expected[0]);
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i)
    {
        size_t index = i + 2;
        CHECK(kLoadoutPrices[index].id == expected[i].id &&
            kLoadoutPrices[index].kind == expected[i].kind &&
            !strcmp(kLoadoutPrices[index].name, expected[i].name) &&
            kLoadoutPrices[index].requisition == expected[i].price);
        Pick(HF_ITEM_AR23_LIBERATOR, 0);
        Put32(history + LOADOUT + expected[i].slot, expected[i].id);
        if (expected[i].slot == STRATAGEM_SLOTS) Put32(history + LOADOUT + STRATAGEM_COUNT, 1);
        Tick();
        wallet_writes = 0;
        uint64_t selected;
        unsigned total;
        int pending;
        CHECK(ReadEquippedItems(&mod, &selected) && selected == (UINT64_C(1) << index));
        RequestCheckout(&mod, (uintptr_t)PickerCard());
        CHECK(SmrTakeCheckoutRequest());
        CHECK(GearCheckout(&total, &pending) && total == expected[i].price && !pending);
        SmrItem item;
        CHECK(SmrShopItem(index, &item) && item.selected && item.price == expected[i].price);
        CHECK(BuyCheckout());
        Tick();
        CHECK(access[index] && saved_wallet == 5000 - expected[i].price &&
            Amount(REQUISITION) == 5000 - expected[i].price);
        CHECK(wallet_writes == 1 && SmrCheckoutComplete());
        CHECK(!GearCheckout(&total, &pending) && total == 0);
        SmrFinishCheckout(0);
        MissionStart(&mod, NULL, NULL);
        CHECK(deployed_access[index]);
        MissionEnd(&mod, NULL, NULL);
        CHECK(!access[index] && !deployed_access[index]);
        SetAmount(REQUISITION, 5000);
    }
    Pick(HF_ITEM_JAR5_DOMINATOR, HF_STRATAGEM_EAGLE_500KG_BOMB);
    Put32(history + LOADOUT + WEAPON_STRIDE, HF_ITEM_GP20_ULTIMATUM);
    Put32(history + LOADOUT + 2 * WEAPON_STRIDE, HF_ITEM_G48_GIGA_GRENADE);
    Put32(history + LOADOUT + STRATAGEM_COUNT, 4);
    Put32(history + LOADOUT + STRATAGEM_SLOTS + STRATAGEM_STRIDE, HF_STRATAGEM_RS422_RAILGUN);
    Put32(history + LOADOUT + STRATAGEM_SLOTS + 2 * STRATAGEM_STRIDE, HF_STRATAGEM_ORBITAL_WALKING_BARRAGE);
    Put32(history + LOADOUT + STRATAGEM_SLOTS + 3 * STRATAGEM_STRIDE, HF_STRATAGEM_ORBITAL_LASER);
    Tick();
    wallet_writes = 0;
    unsigned total;
    int pending;
    CHECK(GearCheckout(&total, &pending) && total == 3500 && !pending);
    OpenCheckout();
    CHECK(BuyCheckout());
    Tick();
    CHECK(saved_wallet == 1500 && Amount(REQUISITION) == 1500 && wallet_writes == 1);
    CHECK(!GearCheckout(&total, &pending) && total == 0);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(!access[i]);
    SmrFinishCheckout(0);
    SetAmount(REQUISITION, 5000);
    size_t jar = PriceIndex("JAR-5 Dominator");
    kLoadoutPrices[jar].requisition = 1375;
    Pick(HF_ITEM_JAR5_DOMINATOR, 0);
    Tick();
    wallet_writes = 0;
    CHECK(GearCheckout(&total, &pending) && total == 1375);
    OpenCheckout();
    CHECK(BuyCheckout());
    Tick();
    CHECK(access[jar] && saved_wallet == 3625 && Amount(REQUISITION) == 3625 && wallet_writes == 1);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    SmrFinishCheckout(0);
    kLoadoutPrices[jar].requisition = 500;
    SetAmount(REQUISITION, 5000);
    Tick();
    wallet_writes = 0;
    printf("PASS expanded catalogue: all %u restrictions gate Ready, deduct once, grant access and expire; seven-item checkout costs 3500\n",
        (unsigned)PRICE_COUNT);
}

static void TestWalletPersistence(void)
{
    ResetExchange();
    const uint32_t credits = kCurrencies[FORGE_CREDITS].id;
    const size_t silo = PriceIndex("MS-11 Solo Silo");
    const unsigned old_eruptor = kLoadoutPrices[0].requisition, old_silo = kLoadoutPrices[silo].requisition;
    kLoadoutPrices[0].requisition = kLoadoutPrices[silo].requisition = 30000;
    SetAmount(REQUISITION, 50000);
    Tick();
    CHECK(BuyItem(0));
    Tick();
    CHECK(saved_wallet == 20000 && access[0]);
    SmrFinishCheckout(0);
    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    Add((uintptr_t)manager, credits, 10, 0, NULL, 0);
    Tick();
    CHECK(saved_currency[FORGE_CREDITS] == 110);
    MissionEnd(&mod, NULL, NULL);
    game_state = HF_STATE_PREPARE_SHIP;
    Add((uintptr_t)manager, REQUISITION, 20000, 0, NULL, 0);
    Add((uintptr_t)manager, kCurrencies[1].id, 1000, 0, NULL, 0);
    Tick();
    CHECK(saved_wallet == 40000 && saved_currency[1] == 500);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(BuyItem(silo));
    Tick();
    CHECK(saved_wallet == 10000 && access[silo]); /* 60,000 spent after a 50,000 starting balance. */
    const int before_reload = wallet_writes;
    Put32(manager + INVENTORY_STATUS, 0);
    Tick();
    SetAmount(REQUISITION, 50000); /* The backend never saw either purchase. */
    SetAmount(credits, 100);
    Put32(manager + INVENTORY_STATUS, 2);
    Tick();
    CHECK(Amount(REQUISITION) == 10000 && Amount(credits) == 110 && wallet_writes == before_reload);
    ledger_open = 0;
    ledger_manager = 0;
    ledger_wallet = (SmrWallet){0};
    SetAmount(REQUISITION, 50000);
    SetAmount(credits, 100);
    Tick();
    Tick();
    CHECK(Amount(REQUISITION) == 10000 && Amount(credits) == 110 && wallet_writes == before_reload);
    kLoadoutPrices[0].requisition = old_eruptor;
    kLoadoutPrices[silo].requisition = old_silo;
    puts("PASS wallet: 60,000 Req spent across a mission payout, Forge Credit pickup, sample cap, and restoration without replay");
}

static uint8_t* KillDescriptor(uint32_t entity)
{
    static uint64_t charger;
    charger = kEnemies[EnemyNamed("Charger")].resource;
    return entity == 11 ? (uint8_t*)&charger : NULL;
}
static void Kill(uint32_t victim, uint64_t creditor)
{
    HfKillEvent event = {.victim = victim, .creditor = creditor};
    EnemyKilled(&mod, &event, NULL);
}
static void TestKillRewards(void)
{
    ResetExchange();
    const uintptr_t old_players = slot_players;
    unsigned char players[0x400] = {0};
    slot_players = (uintptr_t)players;
    entity_descriptor = KillDescriptor;
    const uint64_t titan = kEnemies[EnemyNamed("Bile Titan - Strider")].resource;
    const uint64_t stalker = kEnemies[EnemyNamed("Stalker")].resource;
    size_t plain = 0;
    while (enemy_value[plain] != SMR_KILL_DEFAULT_VALUE) ++plain;
    CHECK(kill_rewards_ready && EnemyIndex(0x1234) < 0);
    SmrKillSummary summary;
    SmrLastKillSummary(&summary);
    uint32_t previous_summary = summary.sequence;

    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    Put32(players + offsetof(HfPlayerManager, count), 2);
    Tick();
    Put32(players + offsetof(HfPlayerManager, count), 0); /* gone by mission end: the sampled count is used */
    EnemySpawned(&mod, &(HfSpawnEvent){.entity = 10, .resource = titan}, NULL);
    EnemySpawned(&mod, &(HfSpawnEvent){.entity = 12, .resource = kEnemies[plain].resource}, NULL);
    EnemySpawned(&mod, &(HfSpawnEvent){.entity = 13, .resource = stalker}, NULL);
    EnemySpawned(&mod, &(HfSpawnEvent){.entity = 14, .resource = 0x1234}, NULL);
    EnemySpawned(&mod, &(HfSpawnEvent){.entity = 15, .resource = titan}, NULL);
    Kill(10, local.peer);
    Kill(10, 456);         /* the same victim pays once */
    Kill(11, 456);         /* a teammate's Charger, identified by its descriptor */
    Kill(12, local.peer);  /* unlisted: the default value */
    Kill(13, local.peer);  /* valued 0 */
    Kill(14, local.peer);  /* not an enemy */
    Kill(15, 0);           /* nobody credited */
    CHECK(kill_count == 3);
    SmrKillNotice notice;
    SmrLastKill(&notice);
    /* The card's last kill was my unlisted one: 1 more for the team and for my bonus. */
    CHECK(notice.sequence == 3 && notice.base == 10000 && notice.before == 10325 && notice.after == 10327);
    MissionEnd(&mod, NULL, NULL);
    /* Team 601 split 2 ways is 301 (rounded up), plus 5% of my 501 = 26 (rounded up). */
    CHECK(kill_reward_due == 327 && !kill_count && Amount(REQUISITION) == 10000);
    SmrLastKillSummary(&summary);
    CHECK(summary.sequence && summary.sequence != previous_summary && summary.team_kills == 3 &&
        summary.own_kills == 2 && summary.players == 2 && summary.team == 601 && summary.mine == 501 &&
        summary.share == 301 && summary.bonus == 26);
    previous_summary = summary.sequence;
    Kill(15, local.peer);  /* not in a mission */
    CHECK(!kill_count);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(!kill_reward_due && Amount(REQUISITION) == 10327 && saved_wallet == 10327 && wallet_writes == 1);
    SmrLastKillSummary(&summary);
    CHECK(summary.sequence == previous_summary && summary.share + summary.bonus == 327);

    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    Put32(players + offsetof(HfPlayerManager, count), 1);
    EnemySpawned(&mod, &(HfSpawnEvent){.entity = 10, .resource = titan}, NULL);
    Kill(10, local.peer);
    MissionEnd(&mod, NULL, NULL);
    CHECK(kill_reward_due == 500); /* solo: no bonus */
    SmrLastKillSummary(&summary);
    CHECK(summary.sequence != previous_summary && summary.team_kills == 1 && summary.own_kills == 1 &&
        summary.players == 1 && summary.share == 500 && summary.bonus == 0);
    previous_summary = summary.sequence;
    SetAmount(REQUISITION, 49990);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(!kill_reward_due && Amount(REQUISITION) == 50000 && saved_wallet == 50000 && !shop_failed);
    SmrLastKillSummary(&summary);
    CHECK(summary.sequence == previous_summary && summary.share == 500);

    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    SmrLastKillSummary(&summary);
    CHECK(summary.sequence != previous_summary && !summary.team_kills && !summary.own_kills &&
        !summary.team && !summary.mine && !summary.share && !summary.bonus && !kill_reward_due);

    entity_descriptor = NULL;
    slot_players = old_players;
    ResetExchange();
    SetAmount(REQUISITION, 5000);
    Tick();
    wallet_writes = 0;
    puts("PASS kill rewards: unique enemy kills split across the team with a 5% contribution bonus, paid on the ship");
}

int main(void)
{
    size_t silo = PriceIndex("MS-11 Solo Silo");
    image = VirtualAlloc(NULL, 0x3480000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    manager = calloc(1, 0xE0000);
    history = calloc(1, 0x2D204);
    profile = calloc(1, 0x200);
    CHECK(image && manager && history && profile);
    PutPointer(INVENTORY_CELL, manager);
    PutPointer(HISTORY_CELL, history);
    PutPointer(0x347CDD8, profile);
    Put32(manager + INVENTORY_COUNT, CURRENCY_COUNT);
    Put32(manager + CATALOGUE_STATUS, 2);
    Put32(manager + INVENTORY_STATUS, 2);
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
        Put32(manager + INVENTORY_RECORDS + i * INVENTORY_STRIDE + 4, kCurrencies[i].id);
    api.version = 16; api.size = sizeof api; api.log = Log; api.read = Read;
    api.module_base = Base; api.build_id = Build; api.game_state = State; api.code_matches = Matches;
    api.global = Global; api.subscribe = Subscribe; api.on_game_thread = OnTick;
    api.hook = Hook;
    api.logs_folder = Logs;
    api.session = Session; api.get_live = GetLive; api.set_live = SetLive;
    api.get_stat = GetStat; api.set_stat = SetStat;
    api.heal = Heal; api.heal_limb = HealLimb; api.limb_state = LimbState;
    prices_refused = 1;
    CHECK(SmrInit(&api, &mod) != 0 && !subscription_calls);
    prices_refused = 0;
    CHECK(SmrInit(&api, &mod) == 0);
    pins_match = 0;
    Tick();
    CHECK(!game_base && !inventory_ready);
    pins_match = 1;
    game_base = (uintptr_t)image;
    inventory_remove = Remove;
    inventory_add = Add;
    SetAmount(REQUISITION, 5000);
    local.peer = 123;
    PutPointer(READY_MENU, picker_menu);
    uintptr_t screen = (uintptr_t)picker_screen;
    memcpy(picker_menu + READY_SCREEN, &screen, sizeof screen);
    Put32(picker_menu, 11);
    memcpy(PickerCard() + READY_PEER, &local.peer, sizeof local.peer);
    SetPickerSelection(history + LOADOUT);
    memcpy(history, &local.peer, sizeof local.peer);
    uint64_t other = 456;
    memcpy(history + RECORD_STRIDE, &other, sizeof other);
    Put32(history + HISTORY_COUNT, 2);
    Put32(history + RECORD_STRIDE + LOADOUT, HF_ITEM_R36_ERUPTOR);
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    CHECK(*(uint32_t*)(history + LOADOUT) == HF_ITEM_R36_ERUPTOR);
    CHECK(*(uint32_t*)(history + RECORD_STRIDE + LOADOUT) == HF_ITEM_R36_ERUPTOR);
    CHECK(*(uint32_t*)(history + LOADOUT + STRATAGEM_COUNT) == 1 &&
        *(uint32_t*)(history + LOADOUT + STRATAGEM_SLOTS) == HF_STRATAGEM_MS11_SOLO_SILO &&
        *(uint32_t*)(profile + 0x40) == HF_ITEM_R36_ERUPTOR &&
        *(uint32_t*)(profile + 0x4C) == 0x4FB52679u);
    CHECK(Amount(REQUISITION) == 5000);
    CHECK(!BuyItem(PRICE_COUNT));
    TestReadyGate();
    TestExpandedCatalog();
    TestDestroyer();
    TestUpgradeEffects();
    TestHelldiver();
    TestExtraSlot();
    TestExchanges();
    TestKillRewards();
    TestLoadoutRefresh();
    CHECK(Amount(REQUISITION) == 5000 && !access[0] && !access[silo]);
    SmrLoadoutQuote quote;
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    Tick();
    CHECK(SmrQuoteLoadout(&mod, &quote) && quote.total == 0 && quote.count == 0 && quote.balance == 5000);
    unreadable_at = (uintptr_t)history + LOADOUT + STRATAGEM_COUNT;
    Tick();
    CHECK(!SmrQuoteLoadout(&mod, &quote));
    unreadable_at = 0;
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    CHECK(SmrQuoteLoadout(&mod, &quote) && quote.total == 2750 && quote.count == 2 &&
        quote.balance_ready && quote.balance == 5000);
    CHECK(!strcmp(quote.items[0].name, "R-36 Eruptor") && quote.items[0].due == 750);
    CHECK(!strcmp(quote.items[1].name, "MS-11 Solo Silo") && quote.items[1].due == 2000);
    CHECK(!wallet_writes && !SmrTakeCheckoutRequest());
    unsigned total;
    int pending;
    CHECK(GearCheckout(&total, &pending) && total == 2750 && !pending);
    OpenCheckout();
    CHECK(BuyCheckout());
    CHECK(!BuyCheckout());
    Tick();
    CHECK(access[0] && access[silo] && saved_wallet == 2250 && Amount(REQUISITION) == 2250 && wallet_writes == 1);
    CHECK(SmrQuoteLoadout(&mod, &quote) && quote.total == 0 && quote.count == 2 &&
        quote.items[0].due == 0 && quote.items[1].due == 0 && quote.balance == 2250);
    CHECK(SmrCheckoutComplete());
    SmrFinishCheckout(0);
    CHECK(!GearCheckout(&total, &pending) && !total && !pending);
    access[silo] = 0;
    CHECK(GearCheckout(&total, &pending) && total == 2000);
    OpenCheckout();
    CHECK(BuyCheckout());
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    Tick();
    CHECK(charge.state == CHARGE_IDLE && !access[silo] && wallet_writes == 1);
    CHECK(!SmrCheckoutComplete());
    access[0] = 0;
    SetAmount(REQUISITION, 5000);
    Tick();
    wallet_writes = 0;

    CHECK(BuyItem(0));
    CHECK(!access[0] && Amount(REQUISITION) == 5000);
    CHECK(!BuyItem(0) && !BuyItem(silo));
    WaitFor(CHARGE_IDLE);
    CHECK(wallet_writes == 1 && saved_wallet == 4250);
    CHECK(access[0] && Amount(REQUISITION) == 4250);
    CHECK(access[0] && !access[silo]);
    CHECK(!BuyItem(0));
    game_state = HF_STATE_MISSION;
    CHECK(!BuyItem(silo));
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    MissionStart(&mod, NULL, NULL);
    Tick();
    CHECK(access[0] && deployed_access[0]);
    CHECK(access[0] && !access[silo]);
    HfPlayerEvent spawn = {0};
    spawn.local = 1; spawn.peer = local.peer;
    PlayerSpawn(&mod, &spawn, NULL);
    Tick();
    CHECK(deployed_access[0] && Amount(REQUISITION) == 4250);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[0] && !deployed_access[0]);
    CHECK(!access[0] && !access[silo]);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[0] && !access[silo]);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(Amount(REQUISITION) == 4250);

    CHECK(BuyItem(silo));
    WaitFor(CHARGE_IDLE);
    CHECK(access[silo] && Amount(REQUISITION) == 2250);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    Tick();
    CHECK(access[silo] && !deployed_access[silo]);
    CHECK(!access[0] && access[silo]);
    MissionEnd(&mod, NULL, NULL);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(access[silo] && Amount(REQUISITION) == 2250);
    game_state = HF_STATE_MISSION;
    Pick(HF_ITEM_AR23_LIBERATOR, HF_STRATAGEM_MS11_SOLO_SILO);
    local_ready = 0;
    MissionStart(&mod, NULL, NULL);
    CHECK(waiting_for_spawn && access[silo]);
    Tick();
    local_ready = 1;
    PlayerSpawn(&mod, &spawn, NULL);
    Tick();
    CHECK(!waiting_for_spawn && access[silo] && deployed_access[silo]);
    CHECK(!access[0] && access[silo]);
    PlayerSpawn(&mod, &spawn, NULL);
    CHECK(deployed_access[silo]);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[silo] && !deployed_access[silo]);
    CHECK(!access[0] && !access[silo]);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(Amount(REQUISITION) == 2250);

    CHECK(BuyItem(0));
    game_state = HF_STATE_MISSION;
    Tick();
    CHECK(charge.state == CHARGE_IDLE && wallet_writes == 2 && Amount(REQUISITION) == 2250);
    game_state = HF_STATE_SHIP;
    Tick();
    remove_failed = 1;
    CHECK(BuyItem(0));
    Tick();
    CHECK(shop_failed && !inventory_ready && !access[0] && wallet_writes == 2 && Amount(REQUISITION) == 2250);
    shop_failed = 0;
    charge.state = CHARGE_IDLE;
    remove_failed = 0;
    SetAmount(REQUISITION, 749);
    Tick();
    CHECK(!BuyItem(0));
    Put32(manager + INVENTORY_STATUS, 0);
    Tick();
    unsigned balance;
    CHECK(!SmrBalance(&balance) && !BuyItem(0));
    Put32(manager + INVENTORY_STATUS, 2);
    Tick();
    SmrItem item;
    CHECK(!shop_failed && inventory_ready);
    CHECK(SmrShopItem(0, &item) && item.selected && !BuyItem(0));
    SetAmount(REQUISITION, 2250);
    Tick();
    wallet_refused = 1;
    CHECK(BuyItem(0));
    Tick();
    CHECK(shop_failed && !inventory_ready);
    CHECK(!SmrCheckoutComplete());
    CHECK(!BuyItem(0) && !BuyItem(silo));
    CHECK(!access[0] && Amount(REQUISITION) == 1500 && saved_wallet == 2250);
    puts("PASS native shop: native deduction before wallet save, access after success only, partial failures stop shop, Ready gate and mission lifetime");
    TestWalletPersistence();
    VirtualFree(image, 0, MEM_RELEASE);
    free(manager); free(history); free(profile);
    return 0;
}
