#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/spend_my_resources.c"

struct HfMod { const HfApi* api; };
static HfApi api;
static HfMod mod = {&api};
static unsigned char *image, *manager, *history, *profile;
static HfLocalUser local;
static HfLocalUser* local_pointer = &local;
static uint32_t game_state = HF_STATE_SHIP;
static int local_ready = 1, pins_match = 1, profile_refused, remove_failed, profile_calls;
static unsigned saved_wallet = 5000;
static int prices_refused, subscription_calls;
static int input_result = 1, input_calls, hook_refused;
static uintptr_t last_button, last_input, unreadable_at;
static unsigned saved_currency[CURRENCY_COUNT];
static int add_failed, add_calls, exchange_profile_calls, exchange_profile_refused;

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
static int Matches(uintptr_t at, const char* pin) { return pins_match; }
static uintptr_t Global(HfMod* unused, const char* name) { return local_ready ? (uintptr_t)&local_pointer : 0; }
static int Subscribe(HfMod* unused, uint32_t kind, HfListener listener, void* data, int32_t priority) { subscription_calls++; return 1; }
static int OnTick(HfMod* unused, HfTickCallback callback, void* data) { return 1; }
static int OriginalInput(uintptr_t button, uintptr_t input)
{
    input_calls++;
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
    if (add_failed != 1) SetAmount(id, Amount(id) + amount - (add_failed == 2 ? 1 : 0));
}
int SmrExchangeProfile(const char* logs, uint32_t source, unsigned debit, uint32_t target, unsigned credit,
    char* result, size_t size)
{
    size_t from = 0, to = 0;
    while (from < CURRENCY_COUNT && kCurrencies[from].id != source) ++from;
    while (to < CURRENCY_COUNT && kCurrencies[to].id != target) ++to;
    CHECK(from < CURRENCY_COUNT && to < CURRENCY_COUNT && from != to);
    CHECK(Amount(source) == saved_currency[from] - debit && Amount(target) == saved_currency[to] + credit);
    exchange_profile_calls++;
    if (exchange_profile_refused)
    {
        snprintf(result, size, "Synthetic exchange profile failure.");
        return 0;
    }
    saved_currency[from] -= debit;
    saved_currency[to] += credit;
    snprintf(result, size, "Exchange profile fixture saved.");
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
        prices[i] = kLoadoutPrices[i].requisition;
    }
    return 1;
}
int SmrDeductProfile(const char* logs, uint32_t resource, unsigned cost, char* result, size_t size)
{
    CHECK(resource == REQUISITION && saved_wallet >= cost);
    CHECK(Amount(REQUISITION) == saved_wallet - cost);
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if (charge.mask & (UINT64_C(1) << i)) CHECK(!access[i]);
    profile_calls++;
    if (profile_refused)
    {
        snprintf(result, size, "Synthetic profile write failure.");
        return 0;
    }
    saved_wallet -= cost;
    snprintf(result, size, "Profile fixture saved.");
    return 1;
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

static int BuyItem(size_t i)
{
    if (i >= PRICE_COUNT) return 0;
    if (charge.state == CHARGE_IDLE)
    {
        Pick(i == 0 ? HF_ITEM_R36_ERUPTOR : HF_ITEM_AR23_LIBERATOR,
            i == PriceIndex("MS-11 Solo Silo") ? HF_STRATAGEM_MS11_SOLO_SILO : 0);
        uint64_t selected;
        checkout_valid = SelectedItems(&mod, &selected);
        checkout_mask = selected;
    }
    return SmrBuyCheckout(&mod);
}

static void TestReadyGate(void)
{
    size_t silo = PriceIndex("MS-11 Solo Silo");
    unsigned char* menu = calloc(1, 0xC0);
    unsigned char* screen = calloc(1, READY_CARD + READY_PEER + 8);
    CHECK(menu && screen);
    PutPointer(READY_MENU, menu);
    uintptr_t screen_pointer = (uintptr_t)screen;
    memcpy(menu + READY_SCREEN, &screen_pointer, sizeof screen_pointer);
    Put32(menu, 11);
    uintptr_t card = screen_pointer + READY_CARD, button = card + READY_BUTTON;
    memcpy((void*)(card + READY_PEER), &local.peer, sizeof local.peer);
    Pick(HF_ITEM_AR23_LIBERATOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Put32(history + LOADOUT + STRATAGEM_COUNT, 4);
    unsigned char before[LOADOUT + STRATAGEM_COUNT + 4];
    memcpy(before, history, sizeof before);
    unsigned balance_before = wallet;
    CHECK(!ReadyInput(button, 1234));
    CHECK(SmrTakeCheckoutRequest() && !SmrTakeCheckoutRequest());
    CHECK(!memcmp(before, history, sizeof before) && wallet == balance_before && !access[0] && !access[silo]);
    CHECK(last_button == button && last_input == 1234);
    CHECK(*(uint32_t*)(history + LOADOUT + STRATAGEM_COUNT) == 4);
    access[silo] = 1;
    CHECK(ReadyInput(button, 1234) == 1);
    access[silo] = 0;
    CHECK(ReadyInput(button + 8, 1234) == 1);
    Put32(menu, 0);
    CHECK(ReadyInput(button, 1234) == 1);
    Put32(menu, 11);
    input_result = 0;
    CHECK(!ReadyInput(button, 1234));
    input_result = 2;
    CHECK(ReadyInput(button, 1234) == 2);
    input_result = 1;
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    CHECK(ReadyInput(button, 1234) == 1);
    Put32(history + RECORD_STRIDE + LOADOUT + STRATAGEM_COUNT, 1);
    Put32(history + RECORD_STRIDE + LOADOUT + STRATAGEM_SLOTS, HF_STRATAGEM_MS11_SOLO_SILO);
    CHECK(ReadyInput(button, 1234) == 1);
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    CHECK(!ReadyInput(button, 1234));
    access[0] = 1;
    CHECK(ReadyInput(button, 1234) == 1);
    access[0] = 0;
    local_ready = 0;
    CHECK(!ReadyInput(button, 1234));
    local_ready = 1;
    unreadable_at = (uintptr_t)history + LOADOUT + STRATAGEM_COUNT;
    CHECK(!ReadyInput(button, 1234));
    unreadable_at = 0;
    Put32(history + LOADOUT + STRATAGEM_COUNT, MAX_STRATAGEMS + 1);
    CHECK(!ReadyInput(button, 1234));
    CHECK(input_calls == 13);
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    access[0] = 1;
    auto_ready_selection = 1;
    InterlockedExchange(&auto_ready, 1);
    input_result = 0;
    CHECK(!ReadyInput(button, 1234)); /* Still awaiting checkout closure. */
    SmrFinishCheckout(1);
    CHECK(!ReadyInput(button + 8, 1234) && auto_ready == 2);
    CHECK(ReadyInput(button, 1234) == 1 && auto_ready == 0);
    CHECK(!ReadyInput(button, 1234)); /* Never toggle Ready twice. */
    InterlockedExchange(&auto_ready, 1);
    SmrFinishCheckout(0);
    CHECK(!ReadyInput(button, 1234) && auto_ready == 0);
    InterlockedExchange(&auto_ready, 2);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    CHECK(!ReadyInput(button, 1234) && auto_ready == 0);
    InterlockedExchange(&auto_ready, 2);
    Put32(menu, 0);
    CHECK(!ReadyInput(button, 1234) && auto_ready == 0);
    Put32(menu, 11);
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
    PutPointer(READY_MENU, NULL);
    free(menu);
    free(screen);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
}

static void ResetExchange(void)
{
    const unsigned amounts[] = {10000, 100, 100, 50, 100}, caps[] = {50000, 500, 250, 100, 0};
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        SetAmount(kCurrencies[i].id, amounts[i]);
        saved_currency[i] = amounts[i];
        unsigned ignored;
        uintptr_t record = ItemRecord(&api, (uintptr_t)manager, kCurrencies[i].id, &ignored);
        Put32((unsigned char*)record + 0x18, caps[i]);
    }
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    shop_failed = remove_failed = add_failed = exchange_profile_refused = 0;
    exchange_profile_calls = add_calls = 0;
    game_state = HF_STATE_SHIP;
    Tick();
}

static void TestExchanges(void)
{
    unsigned debit, credit;
    const unsigned points[] = {1, 50, 100, 200, 500};
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
            CHECK(!SmrExchange(&mod, from, to, 1) && exchange_profile_calls == 0);
            CHECK(!SmrBuyCheckout(&mod));
            SmrCurrency item;
            CHECK(SmrCurrencyItem(from, &item) && item.pending);
            Tick();
            CHECK(!exchange.pending && !shop_failed && exchange_profile_calls == 1 && add_calls == 1);
            CHECK(saved_currency[from] == before_from - debit && saved_currency[to] == before_to + credit);
            CHECK(Amount(kCurrencies[from].id) == saved_currency[from] && Amount(kCurrencies[to].id) == saved_currency[to]);
            CHECK(!auto_ready);
            for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(!access[i]);
        }
    ResetExchange();
    CHECK(SmrExchangeQuote(3, 4, 1, &debit, &credit) && debit == 5 && credit == 2);
    CHECK(!SmrExchangeQuote(0, CURRENCY_COUNT, 1, &debit, &credit));
    CHECK(!SmrExchangeQuote(0, 1, 0, &debit, &credit));
    CHECK(!SmrExchangeQuote(0, 4, UINT_MAX, &debit, &credit));
    CHECK(!SmrExchange(&mod, 0, 4, 0));
    CHECK(!SmrExchange(&mod, 0, 4, 21));
    CHECK(!SmrExchange(&mod, 4, 3, 11)); /* 55 super would exceed 100. */
    CHECK(!SmrExchange(&mod, 4, 0, 81)); /* 40,500 Req would exceed 50,000. */
    CHECK(!SmrExchange(&mod, 4, 1, 41)); /* 410 common would exceed 500. */
    CHECK(!SmrExchange(&mod, 4, 2, 31)); /* 155 rare would exceed 250. */
    CHECK(SmrExchange(&mod, 4, 3, 10)); /* Exactly at the cap is allowed. */
    Tick();
    CHECK(Amount(kCurrencies[3].id) == 100);
    ResetExchange();
    SetAmount(REQUISITION, 101);
    saved_currency[0] = 101;
    Tick();
    CHECK(SmrExchange(&mod, 0, 1, 2));
    Tick();
    CHECK(Amount(REQUISITION) == 1 && saved_currency[0] == 1); /* Remainder retained. */
    ResetExchange();
    SetAmount(kCurrencies[4].id, 0);
    saved_currency[4] = 0;
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
    CHECK(!exchange.pending && !exchange_profile_calls && !add_calls && Amount(REQUISITION) == 10000);
    CHECK(!SmrExchange(&mod, 0, 4, 1));
    ResetExchange();
    charge.state = CHARGE_QUEUED;
    CHECK(!SmrExchange(&mod, 0, 4, 1));
    charge.state = CHARGE_IDLE;
    CHECK(SmrExchange(&mod, 0, 4, 2));
    SetAmount(REQUISITION, 999);
    Tick();
    CHECK(!exchange.pending && !exchange_profile_calls && !add_calls && Amount(REQUISITION) == 999);
    ResetExchange();
    CHECK(SmrExchange(&mod, 4, 3, 1));
    SetAmount(kCurrencies[3].id, 100);
    Tick();
    CHECK(!exchange.pending && !exchange_profile_calls && Amount(kCurrencies[4].id) == 100);
    ResetExchange();
    unsigned ignored;
    uintptr_t capped = ItemRecord(&api, (uintptr_t)manager, kCurrencies[3].id, &ignored);
    CHECK(SmrExchange(&mod, 4, 3, 1));
    Put32((unsigned char*)capped + 0x18, 50);
    Tick();
    CHECK(!exchange.pending && !exchange_profile_calls && Amount(kCurrencies[4].id) == 100);
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
        exchange_profile_refused = failure == 2;
        CHECK(SmrExchange(&mod, 0, 4, 1));
        Tick();
        CHECK(shop_failed && !inventory_ready && !exchange_ready && !exchange.pending);
        CHECK(add_calls == (failure == 0 ? 0 : 1));
        CHECK(exchange_profile_calls == (failure == 2 ? 1 : 0));
        CHECK(saved_currency[0] == 10000 && saved_currency[4] == 100);
        CHECK(!SmrExchange(&mod, 0, 4, 1));
        CHECK(!BuyItem(0));
    }
    ResetExchange();
    SetAmount(REQUISITION, 5000);
    saved_wallet = 5000;
    Tick();
    puts("PASS exchanges: all 20 pairs, exact batches, remainders, caps, ship-only, stale balance/cap checks, shared transaction exclusion and partial failures");
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
        {HF_STRATAGEM_MLS4X_COMMANDO, PRICE_STRATAGEM, STRATAGEM_SLOTS, 500, "MLS-4X Commando"}
    };
    unsigned char* card = calloc(1, READY_PEER + 8);
    CHECK(card && PRICE_COUNT == 2 + sizeof expected / sizeof expected[0]);
    memcpy(card + READY_PEER, &local.peer, sizeof local.peer);
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
        uint64_t selected;
        unsigned total;
        int pending;
        CHECK(SelectedItems(&mod, &selected) && selected == (UINT64_C(1) << index));
        CHECK(!ReadyAccessAllowed(&mod, (uintptr_t)card) && SmrTakeCheckoutRequest());
        CHECK(SmrCheckout(&total, &pending) && total == expected[i].price && !pending);
        SmrItem item;
        CHECK(SmrShopItem(index, &item) && item.selected && item.price == expected[i].price);
        CHECK(SmrBuyCheckout(&mod));
        Tick();
        CHECK(access[index] && saved_wallet == 5000 - expected[i].price &&
            Amount(REQUISITION) == 5000 - expected[i].price);
        CHECK(profile_calls == 1 && ReadyAccessAllowed(&mod, (uintptr_t)card));
        CHECK(!SmrCheckout(&total, &pending) && total == 0);
        SmrFinishCheckout(0);
        MissionStart(&mod, NULL, NULL);
        CHECK(deployed_access[index]);
        MissionEnd(&mod, NULL, NULL);
        CHECK(!access[index] && !deployed_access[index]);
        saved_wallet = 5000;
        SetAmount(REQUISITION, 5000);
        profile_calls = 0;
    }
    Pick(HF_ITEM_JAR5_DOMINATOR, HF_STRATAGEM_EAGLE_500KG_BOMB);
    Put32(history + LOADOUT + WEAPON_STRIDE, HF_ITEM_GP20_ULTIMATUM);
    Put32(history + LOADOUT + 2 * WEAPON_STRIDE, HF_ITEM_G48_GIGA_GRENADE);
    Put32(history + LOADOUT + STRATAGEM_COUNT, 4);
    Put32(history + LOADOUT + STRATAGEM_SLOTS + STRATAGEM_STRIDE, HF_STRATAGEM_RS422_RAILGUN);
    Put32(history + LOADOUT + STRATAGEM_SLOTS + 2 * STRATAGEM_STRIDE, HF_STRATAGEM_ORBITAL_WALKING_BARRAGE);
    Put32(history + LOADOUT + STRATAGEM_SLOTS + 3 * STRATAGEM_STRIDE, HF_STRATAGEM_ORBITAL_LASER);
    Tick();
    unsigned total;
    int pending;
    CHECK(SmrCheckout(&total, &pending) && total == 3500 && !pending);
    CHECK(SmrBuyCheckout(&mod));
    Tick();
    CHECK(saved_wallet == 1500 && Amount(REQUISITION) == 1500 && profile_calls == 1);
    CHECK(!SmrCheckout(&total, &pending) && total == 0);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(!access[i]);
    SmrFinishCheckout(0);
    saved_wallet = 5000;
    SetAmount(REQUISITION, 5000);
    profile_calls = 0;
    size_t jar = PriceIndex("JAR-5 Dominator");
    kLoadoutPrices[jar].requisition = 1375;
    Pick(HF_ITEM_JAR5_DOMINATOR, 0);
    Tick();
    CHECK(SmrCheckout(&total, &pending) && total == 1375);
    CHECK(SmrBuyCheckout(&mod));
    Tick();
    CHECK(access[jar] && saved_wallet == 3625 && Amount(REQUISITION) == 3625 && profile_calls == 1);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    SmrFinishCheckout(0);
    kLoadoutPrices[jar].requisition = 500;
    saved_wallet = 5000;
    SetAmount(REQUISITION, 5000);
    profile_calls = 0;
    free(card);
    puts("PASS expanded catalogue: all 47 restrictions gate Ready, deduct once, grant access and expire; seven-item checkout costs 3500");
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
    TestExchanges();
    CHECK(Amount(REQUISITION) == 5000 && !access[0] && !access[silo]);
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    unsigned total;
    int pending;
    CHECK(SmrCheckout(&total, &pending) && total == 2750 && !pending);
    CHECK(SmrBuyCheckout(&mod));
    CHECK(!SmrBuyCheckout(&mod));
    Tick();
    CHECK(access[0] && access[silo] && saved_wallet == 2250 && Amount(REQUISITION) == 2250 && profile_calls == 1);
    CHECK(auto_ready == 1);
    SmrFinishCheckout(0);
    CHECK(!SmrCheckout(&total, &pending) && !total && !pending);
    access[silo] = 0;
    CHECK(SmrCheckout(&total, &pending) && total == 2000);
    CHECK(SmrBuyCheckout(&mod));
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    Tick();
    CHECK(charge.state == CHARGE_IDLE && !access[silo] && profile_calls == 1);
    CHECK(auto_ready == 0);
    access[0] = 0;
    saved_wallet = 5000;
    SetAmount(REQUISITION, 5000);
    profile_calls = 0;
    Tick();

    CHECK(BuyItem(0));
    CHECK(!access[0] && Amount(REQUISITION) == 5000);
    CHECK(!BuyItem(0) && !BuyItem(silo));
    WaitFor(CHARGE_IDLE);
    CHECK(profile_calls == 1 && saved_wallet == 4250);
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
    CHECK(charge.state == CHARGE_IDLE && profile_calls == 2 && Amount(REQUISITION) == 2250);
    game_state = HF_STATE_SHIP;
    Tick();
    remove_failed = 1;
    CHECK(BuyItem(0));
    Tick();
    CHECK(shop_failed && !inventory_ready && !access[0] && profile_calls == 2 && Amount(REQUISITION) == 2250);
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
    profile_refused = 1;
    CHECK(BuyItem(0));
    Tick();
    CHECK(shop_failed && !inventory_ready);
    CHECK(auto_ready == 0);
    CHECK(!BuyItem(0) && !BuyItem(silo));
    CHECK(!access[0] && Amount(REQUISITION) == 1500 && saved_wallet == 2250);
    puts("PASS native shop: native deduction before profile edit, access after success only, partial failures stop shop, Ready gate and mission lifetime");
    VirtualFree(image, 0, MEM_RELEASE);
    free(manager); free(history); free(profile);
    return 0;
}
