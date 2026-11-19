#include "../src/requisition.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int FixtureSpend(const char*, uint32_t, unsigned, char*, size_t);
#define SmrSpendRequisition FixtureSpend
#include "../src/spend_my_resources.c"
#undef SmrSpendRequisition

struct HfMod { const HfApi* api; };
static HfApi api;
static HfMod mod = {&api};
static unsigned char *image, *manager, *history, *profile;
static HfLocalUser local;
static HfLocalUser* local_pointer = &local;
static uint32_t game_state = HF_STATE_SHIP;
static int local_ready = 1, pins_match = 1, refuse_charge, lose_reply, network_attempts;
static const char* fixture_logs;
static int input_result = 1, input_calls, hook_refused;
static uintptr_t last_button, last_input, unreadable_at;

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
static int Read(uintptr_t at, void* out, size_t size) { if (!at || at == unreadable_at) return 1; memcpy(out, (const void*)at, size); return 0; }
static uintptr_t Base(const char* name, size_t* size) { return !strcmp(name, "game.dll") ? (uintptr_t)image : 0; }
static const char* Build(void) { return "25480438"; }
static uint32_t State(HfMod* unused) { return game_state; }
static int Matches(uintptr_t at, const char* pin) { return pins_match; }
static uintptr_t Global(HfMod* unused, const char* name) { return local_ready ? (uintptr_t)&local_pointer : 0; }
static int Subscribe(HfMod* unused, uint32_t kind, HfListener listener, void* data, int32_t priority) { return 1; }
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
static uint32_t AddWeapon(HfMod* unused, const HfWeapon* weapon)
{
    CHECK(weapon->base == HF_ITEM_AR23A_LIBERATOR_CARBINE);
    return 0x12345678u;
}
static const char* Logs(HfMod* unused) { return fixture_logs; }
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
    CHECK(id == REQUISITION || id == token_weapon);
    unsigned before = Amount(id);
    SetAmount(id, before > amount ? before - amount : 0);
}
static int FixtureSpend(const char* logs, uint32_t token, unsigned amount, char* error, size_t error_size)
{
    network_attempts++;
    if (refuse_charge)
    {
        snprintf(error, error_size, "Synthetic refusal.");
        return 0;
    }
    int result = SmrSpendRequisition(logs, token, amount, error, error_size);
    if (lose_reply && result == 1)
    {
        snprintf(error, error_size, "Synthetic lost purchase reply.");
        return -1;
    }
    return result;
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

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    fixture_logs = argv[1];
    image = VirtualAlloc(NULL, 0x3480000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    manager = calloc(1, 0xE0000);
    history = calloc(1, 0x2D204);
    profile = calloc(1, 0x200);
    CHECK(image && manager && history && profile);
    PutPointer(INVENTORY_CELL, manager);
    PutPointer(HISTORY_CELL, history);
    PutPointer(0x347CDD8, profile);
    Put32(manager + INVENTORY_COUNT, 4);
    Put32(manager + CATALOGUE_STATUS, 2);
    Put32(manager + INVENTORY_STATUS, 2);
    uint32_t ids[] = {REQUISITION, HF_ITEM_R36_ERUPTOR, 699439563u, 0x12345678u};
    for (size_t i = 0; i < 4; ++i)
        Put32(manager + INVENTORY_RECORDS + i * INVENTORY_STRIDE + 4, ids[i]);
    api.version = 16; api.size = sizeof api; api.log = Log; api.read = Read;
    api.module_base = Base; api.build_id = Build; api.game_state = State; api.code_matches = Matches;
    api.global = Global; api.subscribe = Subscribe; api.on_game_thread = OnTick;
    api.hook = Hook;
    api.add_weapon = AddWeapon; api.logs_folder = Logs;
    CHECK(SmrInit(&api, &mod) == 0);
    char problem[192] = "";
    CHECK(SmrSpendRequisition(fixture_logs, token_attachment, 999, problem, sizeof problem) == 0);
    CHECK(strstr(problem, "price"));
    CHECK(SmrSpendRequisition(argv[2], token_attachment, 999, problem, sizeof problem) == 0);
    CHECK(strstr(problem, "price"));
    CHECK(SmrSpendRequisition(fixture_logs, 0xDEADBEEFu, 1000, problem, sizeof problem) == 0);
    pins_match = 0;
    Tick();
    CHECK(!game_base && !inventory_ready);
    pins_match = 1;
    game_base = (uintptr_t)image;
    inventory_remove = Remove;
    SetAmount(REQUISITION, 5000);
    SetAmount(HF_ITEM_R36_ERUPTOR, 1);
    SetAmount(699439563u, 1);
    SetAmount(0x12345678u, 1);
    local.peer = 123;
    memcpy(history, &local.peer, sizeof local.peer);
    uint64_t other = 456;
    memcpy(history + RECORD_STRIDE, &other, sizeof other);
    Put32(history + HISTORY_COUNT, 2);
    Put32(history + RECORD_STRIDE + LOADOUT, HF_ITEM_R36_ERUPTOR);
    Pick(HF_ITEM_R36_ERUPTOR, HF_STRATAGEM_MS11_SOLO_SILO);
    Tick();
    CHECK(Amount(HF_ITEM_R36_ERUPTOR) == 1 && Amount(699439563u) == 1);
    CHECK(!Amount(token_weapon));
    CHECK(*(uint32_t*)(history + LOADOUT) == HF_ITEM_R36_ERUPTOR);
    CHECK(*(uint32_t*)(history + RECORD_STRIDE + LOADOUT) == HF_ITEM_R36_ERUPTOR);
    CHECK(*(uint32_t*)(history + LOADOUT + STRATAGEM_COUNT) == 1 &&
        *(uint32_t*)(history + LOADOUT + STRATAGEM_SLOTS) == HF_STRATAGEM_MS11_SOLO_SILO &&
        *(uint32_t*)(profile + 0x40) == HF_ITEM_R36_ERUPTOR &&
        *(uint32_t*)(profile + 0x4C) == 0x4FB52679u);
    CHECK(Amount(REQUISITION) == 5000);
    CHECK(!SmrBuyAccess(&mod, PRICE_COUNT));
    TestReadyGate();

    CHECK(SmrBuyAccess(&mod, 0));
    CHECK(!access[0] && Amount(REQUISITION) == 5000);
    CHECK(!SmrBuyAccess(&mod, 0) && !SmrBuyAccess(&mod, 1));
    WaitFor(CHARGE_IDLE);
    CHECK(network_attempts == 1);
    CHECK(access[0] && Amount(REQUISITION) == 4000 && Amount(HF_ITEM_R36_ERUPTOR) == 1);
    CHECK(access[0] && !access[1]);
    CHECK(!SmrBuyAccess(&mod, 0));
    game_state = HF_STATE_MISSION;
    CHECK(!SmrBuyAccess(&mod, 1));
    Pick(HF_ITEM_R36_ERUPTOR, 0);
    MissionStart(&mod, NULL, NULL);
    Tick();
    CHECK(access[0] && deployed_access[0] && Amount(HF_ITEM_R36_ERUPTOR) == 1);
    CHECK(access[0] && !access[1]);
    HfPlayerEvent spawn = {0};
    spawn.local = 1; spawn.peer = local.peer;
    PlayerSpawn(&mod, &spawn, NULL);
    Tick();
    CHECK(deployed_access[0] && Amount(REQUISITION) == 4000);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[0] && !deployed_access[0]);
    CHECK(!access[0] && !access[1]);
    MissionEnd(&mod, NULL, NULL);
    CHECK(!access[0] && !access[1]);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(Amount(HF_ITEM_R36_ERUPTOR) == 1 && Amount(REQUISITION) == 4000);

    CHECK(SmrBuyAccess(&mod, 1));
    WaitFor(CHARGE_IDLE);
    CHECK(access[1] && Amount(699439563u) == 1 && Amount(REQUISITION) == 3000);
    Pick(HF_ITEM_AR23_LIBERATOR, 0);
    game_state = HF_STATE_MISSION;
    MissionStart(&mod, NULL, NULL);
    Tick();
    CHECK(access[1] && !deployed_access[1]);
    CHECK(!access[0] && access[1]);
    MissionEnd(&mod, NULL, NULL);
    game_state = HF_STATE_SHIP;
    Tick();
    CHECK(access[1] && Amount(699439563u) == 1 && Amount(REQUISITION) == 3000);
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
    CHECK(Amount(699439563u) == 1 && Amount(REQUISITION) == 3000);

    refuse_charge = 1;
    CHECK(SmrBuyAccess(&mod, 0));
    WaitFor(CHARGE_IDLE);
    CHECK(!access[0] && Amount(HF_ITEM_R36_ERUPTOR) == 1 && Amount(REQUISITION) == 3000);
    SetAmount(REQUISITION, 999);
    Tick();
    CHECK(!SmrBuyAccess(&mod, 0));
    Put32(manager + INVENTORY_STATUS, 0);
    Tick();
    unsigned balance;
    CHECK(!SmrBalance(&balance) && !SmrBuyAccess(&mod, 0));
    Put32(manager + INVENTORY_STATUS, 2);
    SetAmount(HF_ITEM_R36_ERUPTOR, 0);
    Tick();
    SmrItem item;
    CHECK(!shop_failed && inventory_ready && !Amount(HF_ITEM_R36_ERUPTOR));
    CHECK(SmrShopItem(0, &item) && !item.owned && !SmrBuyAccess(&mod, 0));
    SetAmount(HF_ITEM_R36_ERUPTOR, 1);
    refuse_charge = 0;
    SetAmount(REQUISITION, 3000);
    Tick();
    lose_reply = 1;
    CHECK(SmrBuyAccess(&mod, 0));
    WaitFor(CHARGE_UNKNOWN);
    CHECK(!SmrBuyAccess(&mod, 0) && !SmrBuyAccess(&mod, 1));
    CHECK(!access[0] && Amount(REQUISITION) == 3000);
    puts("PASS native shop: real HTTP token purchases, wallet delta, native ownership/loadouts preserved, mission lifetime, spawn retry, refusal");
    VirtualFree(image, 0, MEM_RELEASE);
    free(manager); free(history); free(profile);
    return 0;
}
