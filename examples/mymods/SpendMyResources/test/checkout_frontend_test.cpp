#include "../src/shop_frontend.cpp"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
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
static bool upgradeRequested, upgradeContinued, checkoutPaid;
static int purchaseCalls;
static SmrDestroyerStatus upgrade = {{0, 40, 20, 3, 50}, 1, 1, SMR_UPGRADE_NONE, SMR_UPGRADE_NONE, 0};
static const SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT] = {
    {}, {{0, 10, 5, 1, 10}, -66, 50, 0, 0, 0}, {{0, 10, 5, 1, 10}, 0, 50, 20, 2, 30},
    {{0, 10, 5, 1, 10}, -66, 50, 0, 0, 0}, {{0, 2, 0, 1, 7}, -62.5, -25, 0, 0, 0},
    {{0, 10, 5, 1, 50}, 0, 10, 0, 0, 0},
};
static constexpr SmrExtraChoice extraChoices[] = {
    {HF_STRATAGEM_EAGLE_500KG_BOMB, "Eagle 500kg Bomb", HF_CATEGORY_OFFENSIVE},
    {HF_STRATAGEM_EAGLE_AIRSTRIKE, "Eagle Airstrike", HF_CATEGORY_OFFENSIVE},
    {HF_STRATAGEM_MS11_SOLO_SILO, "MS-11 Solo Silo", HF_CATEGORY_SUPPLY},
    {HF_STRATAGEM_AG16_GATLING_SENTRY, "A/G-16 Gatling Sentry", HF_CATEGORY_DEFENSIVE},
};
static bool quoteValid = true;
static SmrLoadoutQuote loadoutQuote = {2000, 1000, 1, 2, {{"R-36 Eruptor", 0}, {"MS-11 Solo Silo", 2000}}};
const char* g_hellforgePixelShader = nullptr;
static int FixtureRead(uintptr_t at, void* out, size_t size) { memcpy(out, (void*)at, size); return 0; }
static uintptr_t FixtureBase(const char*, size_t*) { return (uintptr_t)image; }
static uint32_t FixtureState(HfMod*) { return fixtureState; }
static void Show(bool on) { modalShown = on; }
static void Close() { windowClosed = true; }

extern "C" int SmrInit(const HfApi*, HfMod*) { return 0; }
extern "C" void SmrFinishCheckout(int ready)
{
    if (ready) CHECK(!modalShown);
    else { upgrade.selected = SMR_UPGRADE_NONE; upgrade.extra = 0; }
    checkoutPaid = false;
    automaticReady = ready != 0;
}
extern "C" int SmrTakeCheckoutRequest() { bool value = requested; requested = false; return value; }
extern "C" int SmrTakeDestroyerRequest() { bool value = upgradeRequested; upgradeRequested = false; return value; }
extern "C" void SmrFinishDestroyer(int proceed) { if (proceed) CHECK(!modalShown); upgradeContinued = proceed != 0; }
extern "C" SmrDestroyerStatus SmrDestroyer(HfMod*) { return upgrade; }
extern "C" const SmrUpgradeTuning* SmrGetUpgradeTuning(SmrUpgrade choice)
{
    return choice > SMR_UPGRADE_NONE && choice < SMR_UPGRADE_COUNT ? &tuning[choice] : nullptr;
}
extern "C" int SmrSelectUpgrade(HfMod*, SmrUpgrade selected, uint32_t extra)
{
    upgrade.selected = selected;
    upgrade.extra = extra;
    return 1;
}
extern "C" const char* SmrExtraName(uint32_t type)
{
    for (const auto& choice : extraChoices)
        if (choice.type == type) return choice.name;
    return nullptr;
}
extern "C" size_t SmrExtraChoices(SmrExtraChoice* choices, size_t capacity)
{
    size_t count = capacity < IM_ARRAYSIZE(extraChoices) ? capacity : IM_ARRAYSIZE(extraChoices);
    memcpy(choices, extraChoices, count * sizeof choices[0]);
    return count;
}
extern "C" int SmrBalance(unsigned* balance) { *balance = 5000; return 1; }
extern "C" int SmrQuoteLoadout(SmrLoadoutQuote* out) { *out = loadoutQuote; return quoteValid; }
extern "C" int SmrCheckout(unsigned* total, unsigned upgrade_price[CURRENCY_COUNT], int* pending)
{
    *total = due;
    memcpy(upgrade_price, tuning[upgrade.selected].price, sizeof tuning[0].price);
    *pending = transactionPending;
    return (due || upgrade_price[FORGE_CREDITS]) && !transactionPending;
}
extern "C" int SmrCheckoutPaid() { return checkoutPaid; }
extern "C" int SmrBuyCheckout(HfMod*) { ++purchaseCalls; transactionPending = 1; return 1; }
extern "C" void SmrMessage(char* text, size_t size)
{
    snprintf(text, size, "%s", editorapp::destroyerOpen ?
        "Choose an upgrade or skip to continue." : "Checkout fixture");
}
extern "C" int SmrShopItem(size_t i, SmrItem* item)
{
    if (i > 1 || !due) return 0;
    *item = {i ? "MS-11 Solo Silo" : "R-36 Eruptor", i ? 2000u : 750u, 0, 0, 1};
    return 1;
}
extern "C" int SmrCurrencyItem(size_t i, SmrCurrency* item)
{
    const char* names[] = {"Requisition slips", "Common samples", "Rare samples", "Super samples", "Forge Credits"};
    const unsigned points[] = {1, 50, 100, 200, 750};
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

static void CheckCostPanel(const char* total, bool breakdown)
{
    CHECK(editorapp::NoticeShown());
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        editorapp::Overlay(nullptr, 1280, 720);
        const char* text = GImGui->LogBuffer.c_str();
        CHECK(strstr(text, total));
        CHECK((strstr(text, "R-36 Eruptor - Access ready") != nullptr) == breakdown);
        CHECK((strstr(text, "MS-11 Solo Silo - 2000 Req") != nullptr) == breakdown);
        CHECK((strstr(text, "Available: 1000 Req") != nullptr) == (quoteValid && loadoutQuote.balance_ready));
        CHECK((strstr(text, "Not enough requisition") != nullptr) == breakdown);
        ImGui::LogFinish();
        ImGui::Render();
    }
    ImGuiWindow* panel = ImGui::FindWindowByName("Loadout cost");
    CHECK(panel && (panel->Flags & ImGuiWindowFlags_NoInputs) == ImGuiWindowFlags_NoInputs &&
        ImGui::GetDrawData()->TotalVtxCount > 0);
    CHECK(!modalShown && !transactionPending && !upgrade.selected && !automaticReady);
}

static void SaveUpgradeScreenshot(const char* path, bool expectGameIcons)
{
    const UINT width = static_cast<UINT>(ImGui::GetIO().DisplaySize.x);
    const UINT height = static_cast<UINT>(ImGui::GetIO().DisplaySize.y);
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)));
    CHECK(ImGui_ImplDX11_Init(device, context));
    CHECK(icons::Upload11(device));

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* view = nullptr;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target)));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(target, nullptr, &view)));

    const float background[] = {0.10f, 0.12f, 0.15f, 1.0f};
    context->OMSetRenderTargets(1, &view, nullptr);
    context->ClearRenderTargetView(view, background);
    ImGui_ImplDX11_NewFrame();
    Draw();
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    unsigned atlasIndices = 0;
    const ImDrawData& data = *ImGui::GetDrawData();
    for (const ImDrawList* list : data.CmdLists)
        for (const ImDrawCmd& command : list->CmdBuffer)
            if (command.GetTexID() == icons::Texture()) atlasIndices += command.ElemCount;
    CHECK(atlasIndices == (expectGameIcons ? (4 + IM_ARRAYSIZE(extraChoices)) * 2u * 6u : 0));
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ID3D11Texture2D* readback = nullptr;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)));
    context->CopyResource(readback, target);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped)));
    const auto* pixels = static_cast<const BYTE*>(mapped.pData);
    unsigned drawn = 0;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
            drawn += memcmp(pixels + y * mapped.RowPitch + x * 4, pixels, 3) != 0;
    CHECK(drawn > 10000);

    BITMAPINFOHEADER header = {sizeof header, static_cast<LONG>(width), -static_cast<LONG>(height), 1, 32, BI_RGB};
    constexpr DWORD offset = sizeof(BITMAPFILEHEADER) + sizeof header;
    const DWORD row = width * 4;
    BITMAPFILEHEADER file = {0x4D42, offset + row * height, 0, 0, offset};
    printf("Saving Destroyer Upgrades screenshot: %s\n", path);
    FILE* out = fopen(path, "wb");
    CHECK(out);
    CHECK(fwrite(&file, sizeof file, 1, out) == 1 && fwrite(&header, sizeof header, 1, out) == 1);
    for (UINT y = 0; y < height; ++y)
        CHECK(fwrite(pixels + y * mapped.RowPitch, row, 1, out) == 1);
    CHECK(fclose(out) == 0);

    context->Unmap(readback, 0);
    readback->Release();
    view->Release();
    target->Release();
    ImGui_ImplDX11_Shutdown();
    context->Release();
    device->Release();
}

static void PressModalButton(const char* label)
{
    ImGuiWindow* window = ImGui::FindWindowByName(editorapp::destroyerOpen ?
        "Spend My Resources - Destroyer Upgrades" : "Spend My Resources");
    CHECK(window);
    ImGui::NewFrame();
    GImGui->NavActivateId = GImGui->NavActivateDownId = window->GetID(label);
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
}

static ImRect PressExtraTile(uint32_t type)
{
    ImGuiWindow* modal = ImGui::FindWindowByName("Spend My Resources - Destroyer Upgrades");
    CHECK(modal && modal->DC.ChildWindows.Size == 1);
    ImGuiWindow* grid = modal->DC.ChildWindows[0];
    const ImGuiID id = ImHashStr("##Stratagem", 0, grid->GetID(static_cast<int>(type)));
    ImGui::NewFrame();
    GImGui->NavWindow = grid;
    GImGui->NavId = GImGui->NavActivateId = GImGui->NavActivateDownId = id;
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
    CHECK(editorapp::destroyerExtra == type);
    return grid->NavRectRel[ImGuiNavLayer_Main];
}

static void CheckModalText(const char* text, const char* second = nullptr, const char* third = nullptr)
{
    Draw();
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    CHECK(strstr(GImGui->LogBuffer.c_str(), text));
    if (second) CHECK(strstr(GImGui->LogBuffer.c_str(), second));
    if (third) CHECK(strstr(GImGui->LogBuffer.c_str(), third));
    ImGui::LogFinish();
    ImGui::Render();
}

int main(int argc, char** argv)
{
    if (argc > 3)
    {
        puts("Usage: checkout_frontend_test.exe [screenshot.bmp [HellForge mods folder]]");
        return 2;
    }
    image = (unsigned char*)VirtualAlloc(nullptr, 0x3480000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image);
    uintptr_t pointer = (uintptr_t)&menu;
    memcpy(image + 0x347CE38, &pointer, sizeof pointer);
    fixtureApi.read = FixtureRead; fixtureApi.module_base = FixtureBase; fixtureApi.game_state = FixtureState;
    editorapp::Host host = {};
    host.api = &fixtureApi; host.modal = Show; host.close = Close;
    if (argc == 3) host.modsFolder = argv[2];
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
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureState = HF_STATE_SHIP;
    editorapp::Tick();
    CHECK(!modalShown);
    CheckCostPanel("Loadout cost: 2000 Req", true);
    loadoutQuote.count = 0;
    loadoutQuote.total = 0;
    CheckCostPanel("Loadout cost: 0 Req", false);
    quoteValid = false;
    CheckCostPanel("Loadout cost unavailable", false);
    upgradeRequested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::destroyerOpen && !editorapp::shopOpen && !editorapp::NoticeShown());
    Draw();
    Draw();
    const char* choices[] = {"##Eagle Upgrade", "##Defensive Upgrade", "##Support Upgrade", "##Extra Stratagem Slot"};
    for (size_t i = 0; i < IM_ARRAYSIZE(choices); ++i)
    {
        PressModalButton(choices[i]);
        CHECK(editorapp::destroyerSelection == i + 1);
        CHECK(!upgrade.selected && !purchaseCalls && modalShown);
    }
    PressModalButton("Select upgrade");
    CHECK(!upgrade.selected && !purchaseCalls && modalShown);
    CheckModalText("Choose an extra stratagem", "Cost at checkout:", "Selected: None");
    CheckModalText("50 Forge Credits (50 available)", "10 Common samples (40 available)",
        "1 Super sample (3 available)");
    CheckModalText("5 Rare samples (20 available)");
    CheckModalText("Offensive", "Support", "Defensive");
    const ImRect first = PressExtraTile(HF_STRATAGEM_EAGLE_500KG_BOMB);
    const ImRect second = PressExtraTile(HF_STRATAGEM_EAGLE_AIRSTRIKE);
    CHECK(first.Min.y == second.Min.y && first.Max.x < second.Min.x);
    CheckModalText("Selected: Eagle Airstrike");
    if (argc >= 2) SaveUpgradeScreenshot(argv[1], argc == 3);
    PressModalButton("Select upgrade");
    CHECK(upgrade.selected == SMR_UPGRADE_EXTRA_SLOT && upgrade.extra == HF_STRATAGEM_EAGLE_AIRSTRIKE && !purchaseCalls);
    requested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::shopOpen);
    CheckModalText("Extra stratagem: Eagle Airstrike", "Extra Stratagem Slot",
        "Total due: 2750 Req + 50 Forge Credits + 10 Common samples + 5 Rare samples + 1 Super sample");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Draw();
    CHECK(!modalShown && !upgrade.selected && !upgrade.extra);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    upgradeRequested = true;
    editorapp::Tick();
    Draw();
    PressModalButton("##Orbital Upgrade");
    CHECK(editorapp::destroyerSelection == 0);
    PressModalButton("##Eagle Upgrade");
    PressModalButton("Skip upgrade");
    CHECK(upgradeContinued && !modalShown && !editorapp::destroyerOpen && !automaticReady);
    requested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::shopOpen && !editorapp::NoticeShown());
    Draw();
    Draw();
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    CHECK(ImGui::FindWindowByName("Spend My Resources"));
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Draw();
    CHECK(!modalShown && !editorapp::shopOpen);
    CHECK(!automaticReady);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    upgradeRequested = true;
    editorapp::Tick();
    CHECK(editorapp::destroyerSelection == 0);
    Draw();
    Draw();
    PressModalButton("##Defensive Upgrade");
    CheckModalText("Equipped defensive stratagems: -66% to cooldowns and unlimited uses.", "and Eagle rearm: +50%",
        "10 Forge Credits (50 available)");
    PressModalButton("##Support Upgrade");
    CheckModalText("Equipped support stratagems: -62.5% to cooldowns and unlimited uses.", "and Eagle rearm: -25%",
        "7 Forge Credits (50 available)");
    PressModalButton("Select upgrade");
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT && upgradeContinued && !purchaseCalls && !modalShown && !automaticReady);
    requested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::shopOpen);
    CheckModalText("Total due: 2750 Req + 7 Forge Credits + 2 Common samples + 1 Super sample",
        "2 Common samples (40 available)", "1 Super sample (3 available)");
    upgrade.balance[SUPER_SAMPLES] = 0;
    CheckModalText("1 Super sample (0 available, not enough)");
    upgrade.balance[SUPER_SAMPLES] = 3;
    due = 0;
    CheckModalText("Total due: 7 Forge Credits + 2 Common samples + 1 Super sample");
    PressModalButton("Purchase");
    CHECK(purchaseCalls == 1 && transactionPending && !upgrade.purchased);
    transactionPending = 0;
    checkoutPaid = true;
    upgrade.purchased = upgrade.selected;
    upgrade.selected = SMR_UPGRADE_NONE;
    editorapp::Tick();
    CHECK(!modalShown && !editorapp::shopOpen && automaticReady);
    automaticReady = false;
    due = 2750;
    menu = 11;
    editorapp::Opened();
    CHECK(editorapp::exchangeOpen && !editorapp::shopOpen && !modalShown && !editorapp::NoticeShown());
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
    checkoutPaid = true;
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
    CHECK(!editorapp::NoticeShown());
    CHECK(!automaticReady);
    ImGui::DestroyContext();
    VirtualFree(image, 0, MEM_RELEASE);
    puts("PASS frontend: live read-only loadout costs, upgrade and checkout flow, and ship-only F2 exchange.");
}
