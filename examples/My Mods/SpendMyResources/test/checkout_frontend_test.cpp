#include "../src/shop_frontend.cpp"
#include "imgui_internal.h"
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static bool requested, modalShown;
static unsigned due = 2750;
static int transactionPending;
static bool automaticReady;
static HfApi fixtureApi;
static unsigned char* image;
static uint32_t menu = 11;
const char* g_hellforgePixelShader = nullptr;
static int FixtureRead(uintptr_t at, void* out, size_t size) { memcpy(out, (void*)at, size); return 0; }
static uintptr_t FixtureBase(const char*, size_t*) { return (uintptr_t)image; }
static uint32_t FixtureState(HfMod*) { return HF_STATE_SHIP; }
static void Show(bool on) { modalShown = on; }

extern "C" int SmrInit(const HfApi*, HfMod*) { return 0; }
extern "C" void SmrFinishCheckout(int ready) { if (ready) CHECK(!modalShown); automaticReady = ready != 0; }
extern "C" int SmrTakeCheckoutRequest() { bool value = requested; requested = false; return value; }
extern "C" int SmrBalance(unsigned* balance) { *balance = 5000; return 1; }
extern "C" int SmrCheckout(unsigned* total, int* pending)
{
    *total = due; *pending = transactionPending; return due && !transactionPending;
}
extern "C" int SmrBuyCheckout(HfMod*) { transactionPending = 1; return 1; }
extern "C" void SmrMessage(char* text, size_t size) { snprintf(text, size, "Checkout fixture"); }
extern "C" int SmrShopItem(size_t i, SmrItem* item)
{
    if (i > 1) return 0;
    *item = {i ? "MS-11 Solo Silo" : "R-36 Eruptor", i ? 2000u : 750u, 0, 1, 0, 1};
    return 1;
}

static void Draw()
{
    ImGui::NewFrame();
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
}

int main()
{
    image = (unsigned char*)VirtualAlloc(nullptr, 0x3480000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image);
    uintptr_t pointer = (uintptr_t)&menu;
    memcpy(image + 0x347CE38, &pointer, sizeof pointer);
    fixtureApi.read = FixtureRead; fixtureApi.module_base = FixtureBase; fixtureApi.game_state = FixtureState;
    editorapp::Host host = {};
    host.api = &fixtureApi; host.modal = Show;
    editorapp::Init(host);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    CHECK(!CanOpenOverlay(&fixtureApi, nullptr) && hf_overlay_hotkey == 0);
    editorapp::Tick();
    CHECK(!modalShown);
    requested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::shopOpen);
    Draw();
    Draw();
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    CHECK(ImGui::FindWindowByName("Spend My Resources"));
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Draw();
    CHECK(!modalShown && !editorapp::shopOpen);
    CHECK(!automaticReady);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    requested = true;
    editorapp::Tick();
    CHECK(modalShown);
    editorapp::purchasePending = true;
    due = 0;
    editorapp::Tick();
    CHECK(!modalShown);
    CHECK(automaticReady);
    due = 2750;
    requested = true;
    editorapp::Tick();
    CHECK(modalShown);
    menu = 0;
    editorapp::Tick();
    CHECK(!modalShown);
    CHECK(!automaticReady);
    ImGui::DestroyContext();
    VirtualFree(image, 0, MEM_RELEASE);
    puts("PASS checkout frontend: Ready request opens modal, renders, Escape cancels, payment closes, leaving loadout closes; no hotkey.");
}
