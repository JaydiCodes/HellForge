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

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
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
    CHECK(id == REQUISITION);
    if (remove_failed) return;
    unsigned before = Amount(id);
    SetAmount(id, before > amount ? before - amount : 0);
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
        if (charge.mask & (1u << i)) CHECK(!access[i]);
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
            i == 1 ? HF_STRATAGEM_MS11_SOLO_SILO : 0);
        unsigned selected;
        checkout_valid = SelectedItems(&mod, &selected);
        checkout_mask = selected;
    }
    return SmrBuyCheckout(&mod);
}

static void TestReadyGate(void)
{
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
    CHECK(!memcmp(before, history, sizeof before) && wallet == balance_before && !access[0] && !access[1]);
    CHECK(last_button == button && last_input == 1234);
    CHECK(*(uint32_t*)(history + LOADOUT + STRATAGEM_COUNT) == 4);
    access[1] = 1;
    CHECK(ReadyInput(button, 1234) == 1);
    access[1] = 0;
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

static void TestExpandedCatalog(void)
{
    static const struct { uint32_t id, mix_id; unsigned slot; } expected[] = {
        {HF_ITEM_JAR5_DOMINATOR, HF_ITEM_JAR5_DOMINATOR, 0},
        {HF_ITEM_AR23P_LIBERATOR_PENETRATOR, HF_ITEM_AR23P_LIBERATOR_PENETRATOR, 0},
        {HF_ITEM_GP20_ULTIMATUM, HF_ITEM_GP20_ULTIMATUM, WEAPON_STRIDE},
        {HF_ITEM_P40K_BOLT_PISTOL, HF_ITEM_P40K_BOLT_PISTOL, WEAPON_STRIDE},
        {HF_ITEM_G40K_MELTA_MINE, HF_ITEM_G40K_MELTA_MINE, 2 * WEAPON_STRIDE},
        {HF_ITEM_G48_GIGA_GRENADE, HF_ITEM_G48_GIGA_GRENADE, 2 * WEAPON_STRIDE},
        {HF_STRATAGEM_EAGLE_500KG_BOMB, 3977158988u, STRATAGEM_SLOTS},
        {HF_STRATAGEM_RS422_RAILGUN, 2739946883u, STRATAGEM_SLOTS},
        {HF_STRATAGEM_ORBITAL_WALKING_BARRAGE, 999215554u, STRATAGEM_SLOTS},
        {HF_STRATAGEM_ORBITAL_LASER, 3197981335u, STRATAGEM_SLOTS},
        {HF_STRATAGEM_ARC3_ARC_THROWER, 612174174u, STRATAGEM_SLOTS}
    };
    unsigned char* card = calloc(1, READY_PEER + 8);
    CHECK(card && PRICE_COUNT == 2 + sizeof expected / sizeof expected[0]);
    memcpy(card + READY_PEER, &local.peer, sizeof local.peer);
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i)
    {
        size_t index = i + 2;
        CHECK(kLoadoutPrices[index].id == expected[i].id &&
            kLoadoutPrices[index].mix_id == expected[i].mix_id &&
            kLoadoutPrices[index].requisition == 500);
        Pick(HF_ITEM_AR23_LIBERATOR, 0);
        Put32(history + LOADOUT + expected[i].slot, expected[i].id);
        if (expected[i].slot == STRATAGEM_SLOTS) Put32(history + LOADOUT + STRATAGEM_COUNT, 1);
        Tick();
        unsigned selected, total;
        int pending;
        CHECK(SelectedItems(&mod, &selected) && selected == (1u << index));
        CHECK(!ReadyAccessAllowed(&mod, (uintptr_t)card) && SmrTakeCheckoutRequest());
        CHECK(SmrCheckout(&total, &pending) && total == 500 && !pending);
        SmrItem item;
        CHECK(SmrShopItem(index, &item) && item.selected && item.owned && item.price == 500);
        CHECK(SmrBuyCheckout(&mod));
        Tick();
        CHECK(access[index] && saved_wallet == 4500 && Amount(REQUISITION) == 4500);
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
    kLoadoutPrices[2].requisition = 1375;
    Pick(HF_ITEM_JAR5_DOMINATOR, 0);
    Tick();
    CHECK(SmrCheckout(&total, &pending) && total == 1375);
    CHECK(SmrBuyCheckout(&mod));
    Tick();
    CHECK(access[2] && saved_wallet == 3625 && Amount(REQUISITION) == 3625 && profile_calls == 1);
    MissionStart(&mod, NULL, NULL);
    MissionEnd(&mod, NULL, NULL);
    SmrFinishCheckout(0);
    kLoadoutPrices[2].requisition = 500;
    saved_wallet = 5000;
    SetAmount(REQUISITION, 5000);
    profile_calls = 0;
    free(card);
    puts("PASS expanded catalogue: all new items cost 500, gate Ready, deduct once, grant access and expire; seven-item checkout costs 3500");
}

int main(void)
{
    image = VirtualAlloc(NULL, 0x3480000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    manager = calloc(1, 0xE0000);
    history = calloc(1, 0x2D204);
    profile = calloc(1, 0x200);
    CHECK(image && manager && history && profile);
    PutPointer(INVENTORY_CELL, manager);
    PutPointer(HISTORY_CELL, history);
    PutPointer(0x347CDD8, profile);
    Put32(manager + INVENTORY_COUNT, PRICE_COUNT + 1);
    Put32(manager + CATALOGUE_STATUS, 2);
    Put32(manager + INVENTORY_STATUS, 2);
    Put32(manager + INVENTORY_RECORDS + 4, REQUISITION);
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        Put32(manager + INVENTORY_RECORDS + (i + 1) * INVENTORY_STRIDE + 4, kLoadoutPrices[i].mix_id);
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
    SetAmount(REQUISITION, 5000);
    SetAmount(HF_ITEM_R36_ERUPTOR, 1);
    SetAmount(699439563u, 1);
    for (size_t i = 2; i < PRICE_COUNT; ++i) SetAmount(kLoadoutPrices[i].mix_id, 1);
    local.peer = 123;
    memcpy(history, &local.peer, sizeof local.peer);
    uint64_t other = 456;
    memcpy(history + RECORD_STRIDE, &other, sizeof other);
    Put32(history + HISTORY_COUNT, 2);
    Put32(history + RECORD_STRIDE + LOADOUT, HF_ITEM_R36_ERUPTOR);
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    CHECK(Amount(HF_ITEM_R36_ERUPTOR) == 1 && Amount(699439563u) == 1);
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
    CHECK(Amount(REQUISITION) == 5000 && !access[0] && !access[1]);
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    unsigned total;
    int pending;
    CHECK(SmrCheckout(&total, &pending) && total == 2750 && !pending);
    CHECK(SmrBuyCheckout(&mod));
    CHECK(!SmrBuyCheckout(&mod));
    Tick();
    CHECK(access[0] && access[1] && saved_wallet == 2250 && Amount(REQUISITION) == 2250 && profile_calls == 1);
    CHECK(auto_ready == 1);
    SmrFinishCheckout(0);
    CHECK(!SmrCheckout(&total, &pending) && !total && !pending);
    access[1] = 0;
    CHECK(SmrCheckout(&total, &pending) && total == 2000);
    CHECK(SmrBuyCheckout(&mod));
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    Tick();
    CHECK(charge.state == CHARGE_IDLE && !access[1] && profile_calls == 1);
    CHECK(auto_ready == 0);
    access[0] = 0;
    saved_wallet = 5000;
    SetAmount(REQUISITION, 5000);
    profile_calls = 0;
    Tick();

    CHECK(BuyItem(0));
    CHECK(!access[0] && Amount(REQUISITION) == 5000);
    CHECK(!BuyItem(0) && !BuyItem(1));
    WaitFor(CHARGE_IDLE);
    CHECK(profile_calls == 1 && saved_wallet == 4250);
    CHECK(access[0] && Amount(REQUISITION) == 4250 && Amount(HF_ITEM_R36_ERUPTOR) == 1);
    CHECK(access[0] && !access[1]);
    CHECK(!BuyItem(0));
    game_state = HF_STATE_MISSION;
    CHECK(!BuyItem(1));
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    MissionStart(&mod, NULL, NULL);
    Tick();
    CHECK(access[0] && deployed_access[0] && Amount(HF_ITEM_R36_ERUPTOR) == 1);
    CHECK(access[0] && !access[1]);
    HfPlayerEvent spawn = {0};
    spawn.local = 1; spawn.peer = local.peer;
    PlayerSpawn(&mod, &spawn, NULL);
    Tick();
    CHECK(deployed_access[0] && Amount(REQUISITION) == 4250);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[0] && !deployed_access[0]);
    CHECK(!access[0] && !access[1]);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[0] && !access[1]);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(Amount(HF_ITEM_R36_ERUPTOR) == 1 && Amount(REQUISITION) == 4250);

    CHECK(BuyItem(1));
    WaitFor(CHARGE_IDLE);
    CHECK(access[1] && Amount(699439563u) == 1 && Amount(REQUISITION) == 2250);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    Tick();
    CHECK(access[1] && !deployed_access[1]);
    CHECK(!access[0] && access[1]);
    MissionEnd(&mod, NULL, NULL);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(access[1] && Amount(699439563u) == 1 && Amount(REQUISITION) == 2250);
    game_state = HF_STATE_MISSION;
    Pick(HF_ITEM_AR23_LIBERATOR, HF_STRATAGEM_MS11_SOLO_SILO);
    local_ready = 0;
    MissionStart(&mod, NULL, NULL);
    CHECK(waiting_for_spawn && access[1]);
    Tick();
    CHECK(Amount(699439563u) == 1);
    local_ready = 1;
    PlayerSpawn(&mod, &spawn, NULL);
    Tick();
    CHECK(!waiting_for_spawn && access[1] && deployed_access[1] && Amount(699439563u) == 1);
    CHECK(!access[0] && access[1]);
    PlayerSpawn(&mod, &spawn, NULL);
    CHECK(deployed_access[1]);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[1] && !deployed_access[1]);
    CHECK(!access[0] && !access[1]);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(Amount(699439563u) == 1 && Amount(REQUISITION) == 2250);

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
    SetAmount(HF_ITEM_R36_ERUPTOR, 0);
    Tick();
    SmrItem item;
    CHECK(!shop_failed && inventory_ready && !Amount(HF_ITEM_R36_ERUPTOR));
    CHECK(SmrShopItem(0, &item) && !item.owned && !BuyItem(0));
    SetAmount(HF_ITEM_R36_ERUPTOR, 1);
    SetAmount(REQUISITION, 2250);
    Tick();
    profile_refused = 1;
    CHECK(BuyItem(0));
    Tick();
    CHECK(shop_failed && !inventory_ready);
    CHECK(auto_ready == 0);
    CHECK(!BuyItem(0) && !BuyItem(1));
    CHECK(!access[0] && Amount(REQUISITION) == 1500 && saved_wallet == 2250);
    puts("PASS native shop: native deduction before profile edit, access after success only, partial failures stop shop, Ready gate and mission lifetime");
    VirtualFree(image, 0, MEM_RELEASE);
    free(manager); free(history); free(profile);
    return 0;
}
