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
static uint32_t fixtureState = HF_STATE_SHIP;
static bool windowClosed;
static int exchangeCalls;
const char* g_hellforgePixelShader = nullptr;
static int FixtureRead(uintptr_t at, void* out, size_t size) { memcpy(out, (void*)at, size); return 0; }
static uintptr_t FixtureBase(const char*, size_t*) { return (uintptr_t)image; }
static uint32_t FixtureState(HfMod*) { return fixtureState; }
static void Show(bool on) { modalShown = on; }
static void Close() { windowClosed = true; }

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
extern "C" int SmrCurrencyItem(size_t i, SmrCurrency* item)
{
    const char* names[] = {"Requisition slips", "Common samples", "Rare samples", "Super samples", "Forge Credits"};
    const unsigned points[] = {1, 50, 100, 200, 500};
    if (i >= 5) return 0;
    *item = {names[i], points[i], i ? 10u : 5000u, i ? 500u : 50000u, 1, 0};
    return 1;
}
extern "C" int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit)
{
    *debit = 50 * batches; *credit = batches;
    return from != to && batches;
}
extern "C" int SmrExchange(HfMod*, size_t from, size_t to, unsigned batches)
{
    CHECK(from == 0 && to == 1 && batches == 1);
    exchangeCalls++;
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
    host.api = &fixtureApi; host.modal = Show; host.close = Close;
    editorapp::Init(host);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    CHECK(CanOpenOverlay(&fixtureApi, nullptr) && hf_overlay_hotkey == VK_F2);
    fixtureState = HF_STATE_MISSION;
    CHECK(!CanOpenOverlay(&fixtureApi, nullptr));
    fixtureState = HF_STATE_SHIP;
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
    menu = 11;
    editorapp::Opened();
    CHECK(editorapp::exchangeOpen && !editorapp::shopOpen && !modalShown);
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        editorapp::Frame(nullptr, 1280, 720, 1.0f / 60);
        ImGui::Render();
    }
    CHECK(ImGui::FindWindowByName("Spend My Resources - Currency Exchange"));
    CHECK(!automaticReady && !editorapp::shopOpen);
    ImGuiWindow* exchangeWindow = ImGui::FindWindowByName("Spend My Resources - Currency Exchange");
    ImGui::NewFrame();
    GImGui->NavActivateId = GImGui->NavActivateDownId = exchangeWindow->GetID("Exchange");
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Frame(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
    CHECK(exchangeCalls == 1 && !automaticReady && !editorapp::shopOpen);
    requested = true;
    windowClosed = false;
    editorapp::Tick();
    CHECK(windowClosed && !editorapp::exchangeOpen && editorapp::shopOpen && modalShown);
    windowClosed = false;
    editorapp::Opened(); /* F2 must not cover an active checkout. */
    CHECK(windowClosed && !editorapp::exchangeOpen && editorapp::shopOpen && modalShown);
    editorapp::CloseCheckout();
    editorapp::Opened();
    fixtureState = HF_STATE_MISSION;
    windowClosed = false;
    editorapp::Tick();
    CHECK(windowClosed && !editorapp::exchangeOpen && !editorapp::shopOpen);
    fixtureState = HF_STATE_SHIP;
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
    puts("PASS frontend: Ready checkout and auto-Ready preserved; separate F2 exchange screen renders, ship-only, checkout takes priority.");
}
