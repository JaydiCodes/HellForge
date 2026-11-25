#include "../src/shop/shop_frontend.cpp"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static bool requested, modalShown;
static int modalChanges;
static unsigned due = 2750;
static int transactionPending;
static bool automaticReady;
static HfApi fixtureApi;
static bool pickerOpen = true;
static uint32_t fixtureState = HF_STATE_SHIP;
static bool windowClosed;
static int exchangeCalls;
static bool checkoutComplete;
static int purchaseCalls, continueCalls;
static SmrDestroyerStatus upgrade = {{5000, 40, 20, 3, 50}, 1, 1, SMR_UPGRADE_NONE, SMR_UPGRADE_NONE, 0};
static const SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT] = {
    {}, {{2000, 10, 5, 1, 10}, -66, 50, 0, 0, 0}, {{2000, 10, 5, 1, 10}, 0, 50, 20, 2, 30},
    {{2000, 10, 5, 1, 10}, -66, 50, 0, 0, 0}, {{2000, 2, 0, 1, 7}, -62.5, -25, 0, 0, 0},
    {{2000, 10, 5, 1, 50}, 0, 10, 0, 0, 0},
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
static uint32_t FixtureState(HfMod*) { return fixtureState; }
static void Show(bool on)
{
    CHECK(modalShown != on);
    modalShown = on;
    ++modalChanges;
}
static void Close() { windowClosed = true; }

extern "C" int SmrInit(const HfApi*, HfMod*) { return 0; }
extern "C" void SmrFinishCheckout(int ready)
{
    if (ready) CHECK(!modalShown);
    else { upgrade.selected = SMR_UPGRADE_NONE; upgrade.extra = 0; }
    checkoutComplete = false;
    automaticReady = ready != 0;
}
extern "C" int SmrTakeCheckoutRequest() { bool value = requested; requested = false; return value; }
extern "C" SmrDestroyerStatus SmrDestroyer(HfMod*) { return upgrade; }
static SmrHelldiverStatus helldiver = {{5000, 40, 20, 3, 50}, {1, 1, 1}, 1};
static const SmrHelldiverTuning helldiverTuning = {{0, 50, 0, 0, 0}, 0, 50};
static const SmrHelldiverTuning primaryTradeoffTuning = {{0, 50, 0, 0, 0}, -50, 50};
extern "C" SmrHelldiverStatus SmrHelldiver(HfMod*) { return helldiver; }
extern "C" int SmrSelectHelldiver(HfMod*, SmrHelldiverTier tier, SmrHelldiverPerk perk)
{
    helldiver.selected[tier] = perk;
    return 1;
}
extern "C" const SmrHelldiverTuning* SmrGetHelldiverTuning(SmrHelldiverPerk perk)
{
    if (perk == SMR_PERK_PRIMARY_TRADEOFF) return &primaryTradeoffTuning;
    return perk > SMR_PERK_NONE && perk < SMR_PERK_COUNT ? &helldiverTuning : nullptr;
}
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
extern "C" int SmrBalance(unsigned* balance) { *balance = upgrade.balance[REQUISITION_SLIPS]; return 1; }
extern "C" int SmrLoadoutOpen(HfMod*) { return pickerOpen && fixtureState == HF_STATE_SHIP; }
extern "C" int SmrQuoteLoadout(HfMod*, SmrLoadoutQuote* out) { *out = loadoutQuote; return quoteValid; }
extern "C" SmrCheckoutAction SmrCheckout(unsigned* total, unsigned upgrade_price[CURRENCY_COUNT], int* pending)
{
    *total = due;
    memcpy(upgrade_price, tuning[upgrade.selected].price, sizeof tuning[0].price);
    *pending = transactionPending || checkoutComplete;
    if (*pending || (upgrade.selected && !upgrade.solo) ||
        SmrRequisitionDue(due, upgrade_price) > upgrade.balance[REQUISITION_SLIPS])
        return SMR_CHECKOUT_BLOCKED;
    for (size_t i = COMMON_SAMPLES; i < CURRENCY_COUNT; ++i)
        if (upgrade_price[i] > upgrade.balance[i]) return SMR_CHECKOUT_BLOCKED;
    return due || upgrade.selected ? SMR_CHECKOUT_PURCHASE : SMR_CHECKOUT_CONTINUE;
}
extern "C" int SmrCheckoutComplete() { return checkoutComplete; }
extern "C" int SmrConfirmCheckout(HfMod*, SmrCheckoutAction action)
{
    CHECK(modalShown && action != SMR_CHECKOUT_BLOCKED);
    unsigned total, price[CURRENCY_COUNT];
    int pending;
    CHECK(action == SmrCheckout(&total, price, &pending));
    if (action == SMR_CHECKOUT_CONTINUE)
    {
        ++continueCalls;
        checkoutComplete = true;
    }
    else
    {
        ++purchaseCalls;
        transactionPending = 1;
    }
    return 1;
}
extern "C" void SmrMessage(char* text, size_t size)
{
    snprintf(text, size, "Checkout fixture");
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

static void Draw(float dt = 1.0f / 60)
{
    ImGui::GetIO().DeltaTime = dt;
    ImGui::NewFrame();
    editorapp::Modal(nullptr, 1280, 720, dt);
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

static ImGuiWindow* CheckoutWindow()
{
    ImGuiWindow* window = ImGui::FindWindowByName("Spend My Resources");
    CHECK(window);
    return window;
}

static ImGuiWindow* PanelWindow()
{
    ImGuiWindow* window = CheckoutWindow();
    CHECK(window->DC.ChildWindows.Size == 1);
    return window->DC.ChildWindows[0];
}

static ImRect PressModalButton(const char* label, bool inPanel = false)
{
    ImGuiWindow* window = inPanel ? PanelWindow() : CheckoutWindow();
    ImGui::GetIO().DeltaTime = 1.0f / 60;
    ImGui::NewFrame();
    GImGui->NavWindow = window;
    GImGui->NavId = GImGui->NavActivateId = GImGui->NavActivateDownId = window->GetID(label);
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
    return window->NavRectRel[ImGuiNavLayer_Main];
}

static ImVec2 DrawnUnderlineStart(const ImGuiWindow& window)
{
    ImVec2 start(FLT_MAX, 0);
    for (const ImDrawVert& vertex : window.DrawList->VtxBuffer)
        if (vertex.col == editorapp::kAccent && vertex.pos.x < start.x)
            start = vertex.pos;
    CHECK(start.x != FLT_MAX);
    return start;
}

static unsigned DrawnHoverAlpha(const ImGuiWindow& window)
{
    unsigned strongest = 0;
    for (const ImDrawVert& vertex : window.DrawList->VtxBuffer)
        if (vertex.col != editorapp::kAccent &&
            (vertex.col & ~IM_COL32_A_MASK) == (editorapp::kAccent & ~IM_COL32_A_MASK))
        {
            const unsigned alpha = vertex.col >> IM_COL32_A_SHIFT;
            if (alpha > strongest) strongest = alpha;
        }
    return strongest;
}

static void CheckAnimatedTabs()
{
    const ImGuiWindow& window = *CheckoutWindow();
    const ImVec2 destroyer = DrawnUnderlineStart(window);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(destroyer.x + 40, destroyer.y - 7);
    Draw(0.02f);
    const unsigned earlyHover = DrawnHoverAlpha(window);
    Draw(0.05f);
    const unsigned laterHover = DrawnHoverAlpha(window);
    CHECK(earlyHover > 0 && laterHover > earlyHover);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    Draw(0.02f);
    CHECK(DrawnHoverAlpha(window) > 0 && DrawnHoverAlpha(window) < laterHover);
    Draw(0.20f);
    CHECK(DrawnHoverAlpha(window) == 0);

    PressModalButton("Purchase##tab");
    CHECK(editorapp::checkoutPanel == editorapp::CheckoutPanel::Purchase);
    const ImVec2 moving = DrawnUnderlineStart(window);
    Draw(0.20f);
    const ImVec2 purchase = DrawnUnderlineStart(window);
    CHECK(destroyer.x < moving.x && moving.x < purchase.x);
    CHECK(destroyer.y == moving.y && moving.y == purchase.y);
    CHECK(!purchaseCalls && !continueCalls && modalShown);
    PressModalButton("Destroyer");
    Draw(0.20f);
    CHECK(DrawnUnderlineStart(window).x == destroyer.x);
    Draw();
}

static ImRect PressExtraTile(uint32_t type)
{
    ImGuiWindow* panel = PanelWindow();
    CHECK(panel->DC.ChildWindows.Size == 1);
    ImGuiWindow* grid = panel->DC.ChildWindows[0];
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

static void PressHelldiverPerk(SmrHelldiverTier tier, SmrHelldiverPerk perk)
{
    ImGuiWindow* panel = PanelWindow();
    const ImGuiID tierId = ImHashData(&tier, sizeof(int), panel->ID);
    const int perkIndex = perk;
    const ImGuiID id = ImHashStr(editorapp::kPerkNames[perk], 0, ImHashData(&perkIndex, sizeof perkIndex, tierId));
    ImGui::NewFrame();
    GImGui->NavWindow = panel;
    GImGui->NavId = GImGui->NavActivateId = GImGui->NavActivateDownId = id;
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
}

static void CheckHelldiverTab()
{
    PressModalButton("Helldiver");
    CHECK(editorapp::checkoutPanel == editorapp::CheckoutPanel::Helldiver);
    Draw(0.20f);
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    const char* text = GImGui->LogBuffer.c_str();
    CHECK(strstr(text, "Primary Ammo Trade-off") && strstr(text, "Primary magazines: -50%") &&
        strstr(text, "Secondary magazines: +50%") && !strstr(text, "Deploy without") && !strstr(text, "Stim heal duration"));
    ImGui::LogFinish();
    ImGui::Render();
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_LIGHT_KIT);
    PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_FIELD_MEDIC);
    CHECK(helldiver.selected[SMR_TIER_PLUS] == SMR_PERK_LIGHT_KIT && helldiver.selected[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC &&
        !helldiver.selected[SMR_TIER_ELITE]);
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF);
    CHECK(helldiver.selected[SMR_TIER_PLUS] == SMR_PERK_PRIMARY_TRADEOFF);
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF);
    CHECK(!helldiver.selected[SMR_TIER_PLUS]);
    PressModalButton("Remove upgrades");
    CHECK(!helldiver.selected[SMR_TIER_SUPER] && editorapp::checkoutPanel == editorapp::CheckoutPanel::Helldiver);
    PressModalButton("Destroyer");
    Draw(0.20f);
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
    fixtureApi.game_state = FixtureState;
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
    requested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::shopOpen && !editorapp::NoticeShown());
    Draw();
    Draw();
    CheckAnimatedTabs();
    CheckHelldiverTab();
    ImGuiWindow* checkoutWindow = CheckoutWindow();
    const ImVec2 checkoutPos = checkoutWindow->Pos;
    const int opened = modalChanges;
    const char* choices[] = {"##Eagle Upgrade", "##Defensive Upgrade", "##Support Upgrade", "##Extra Stratagem Slot"};
    for (size_t i = 0; i < IM_ARRAYSIZE(choices); ++i)
    {
        PressModalButton(choices[i], true);
        CHECK(editorapp::destroyerSelection == i + 1);
        CHECK(upgrade.selected == (i == 3 ? SMR_UPGRADE_NONE : editorapp::kDestroyerUpgrades[i + 1].upgrade));
        CHECK(editorapp::checkoutPanel == editorapp::CheckoutPanel::Destroyer && !purchaseCalls && modalShown);
    }
    PressModalButton("Review purchase");
    CHECK(!upgrade.selected && !purchaseCalls && modalShown);
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(!editorapp::destroyerSelection && !editorapp::destroyerExtra && !upgrade.selected);
    PressModalButton("##Extra Stratagem Slot", true);
    CheckModalText("Choose an extra stratagem", "Cost at checkout:", "Selected: None");
    CheckModalText("50 Forge Credits (50 available)", "10 Common samples (40 available)",
        "1 Super sample (3 available)");
    CheckModalText("5 Rare samples (20 available)", "2000 Requisition slips (5000 available)");
    CheckModalText("Offensive", "Support", "Defensive");
    const ImRect first = PressExtraTile(HF_STRATAGEM_EAGLE_500KG_BOMB);
    CHECK(upgrade.selected == SMR_UPGRADE_EXTRA_SLOT && upgrade.extra == HF_STRATAGEM_EAGLE_500KG_BOMB);
    const ImRect second = PressExtraTile(HF_STRATAGEM_EAGLE_AIRSTRIKE);
    CHECK(upgrade.selected == SMR_UPGRADE_EXTRA_SLOT && upgrade.extra == HF_STRATAGEM_EAGLE_AIRSTRIKE);
    CHECK(first.Min.y == second.Min.y && first.Max.x < second.Min.x);
    CheckModalText("Selected: Eagle Airstrike");
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(!upgrade.selected && !upgrade.extra && !editorapp::destroyerExtra);
    PressModalButton("##Extra Stratagem Slot", true);
    CHECK(!upgrade.selected && !editorapp::destroyerExtra);
    PressExtraTile(HF_STRATAGEM_EAGLE_AIRSTRIKE);
    if (argc >= 2) SaveUpgradeScreenshot(argv[1], argc == 3);
    const ImRect select = PressModalButton("Review purchase");
    CHECK(select.Max.y > select.Min.y && select.Max.y + checkoutPos.y <= checkoutWindow->InnerClipRect.Max.y);
    CHECK(upgrade.selected == SMR_UPGRADE_EXTRA_SLOT && upgrade.extra == HF_STRATAGEM_EAGLE_AIRSTRIKE && !purchaseCalls);
    CheckModalText("Extra stratagem: Eagle Airstrike", "Extra Stratagem Slot",
        "Total due: 4750 Req + 50 Forge Credits + 10 Common samples + 5 Rare samples + 1 Super sample");
    PressModalButton("Destroyer");
    CheckModalText("Selected for purchase: Extra Stratagem Slot", "Selected: Eagle Airstrike");
    PressExtraTile(HF_STRATAGEM_EAGLE_500KG_BOMB);
    CHECK(upgrade.extra == HF_STRATAGEM_EAGLE_500KG_BOMB);
    PressModalButton("Review purchase");
    CheckModalText("Extra stratagem: Eagle 500kg Bomb");
    PressModalButton("Destroyer");
    PressModalButton("##Defensive Upgrade", true);
    CheckModalText("Equipped defensive stratagems: -66% to cooldowns and unlimited uses.", "and Eagle rearm: +50%",
        "10 Forge Credits (50 available)");
    PressModalButton("##Support Upgrade", true);
    CheckModalText("Equipped support stratagems: -62.5% to cooldowns and unlimited uses.", "and Eagle rearm: -25%",
        "7 Forge Credits (50 available)");
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT && !upgrade.extra && !purchaseCalls);
    PressModalButton("##Support Upgrade", true);
    CHECK(!upgrade.selected);
    PressModalButton("##Support Upgrade", true);
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT);
    PressModalButton("Review purchase");
    CheckModalText("Total due: 4750 Req + 7 Forge Credits + 2 Common samples + 1 Super sample",
        "2 Common samples (40 available)", "1 Super sample (3 available)");
    upgrade.balance[REQUISITION_SLIPS] = 4749;
    CheckModalText("Not enough requisition for this purchase.", "2000 Requisition slips (4749 available)");
    PressModalButton("Purchase");
    CHECK(!purchaseCalls);
    upgrade.balance[REQUISITION_SLIPS] = 5000;
    upgrade.balance[SUPER_SAMPLES] = 0;
    CheckModalText("1 Super sample (0 available, not enough)");
    PressModalButton("Purchase");
    CHECK(!purchaseCalls);
    upgrade.balance[SUPER_SAMPLES] = 3;
    PressModalButton("Destroyer");
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(!upgrade.selected && !upgrade.extra && !editorapp::destroyerExtra);
    PressModalButton("Purchase##tab");
    CheckModalText("Total due: 2750 Req");
    CHECK(modalShown && modalChanges == opened && !purchaseCalls && !automaticReady);
    CHECK(CheckoutWindow() == checkoutWindow && checkoutWindow->Pos.x == checkoutPos.x && checkoutWindow->Pos.y == checkoutPos.y);
    PressModalButton("Destroyer");
    PressModalButton("##Support Upgrade", true);
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Draw();
    CHECK(!modalShown && !editorapp::shopOpen && !upgrade.selected && !upgrade.extra && !automaticReady);
    io.AddKeyEvent(ImGuiKey_Escape, false);

    upgrade.solo = upgrade.available = 0;
    due = 0;
    requested = true;
    editorapp::Tick();
    CheckModalText("Destroyer upgrades are available in solo play only.");
    PressModalButton("##Support Upgrade", true);
    CHECK(!upgrade.selected);
    PressModalButton("Purchase##tab");
    CheckModalText("Nothing to purchase.");
    PressModalButton("Continue to Ready");
    CHECK(continueCalls == 1 && !purchaseCalls && modalShown && !automaticReady);
    editorapp::Tick();
    CHECK(!modalShown && automaticReady);
    automaticReady = false;

    upgrade.solo = upgrade.available = 1;
    requested = true;
    editorapp::Tick();
    CHECK(editorapp::destroyerSelection == 0 && !editorapp::destroyerExtra);
    Draw();
    PressModalButton("##Support Upgrade", true);
    PressModalButton("Review purchase");
    due = 0;
    CheckModalText("Total due: 2000 Req + 7 Forge Credits + 2 Common samples + 1 Super sample");
    PressModalButton("Purchase");
    CHECK(purchaseCalls == 1 && transactionPending && !upgrade.purchased);
    PressModalButton("Destroyer");
    CHECK(editorapp::checkoutPanel == editorapp::CheckoutPanel::Purchase);
    editorapp::checkoutPanel = editorapp::CheckoutPanel::Destroyer;
    Draw();
    PressModalButton("##Orbital Upgrade", true);
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT && !upgrade.extra);
    editorapp::checkoutPanel = editorapp::CheckoutPanel::Purchase;
    Draw();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Draw();
    CHECK(modalShown && purchaseCalls == 1);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    transactionPending = 0;
    checkoutComplete = true;
    upgrade.purchased = upgrade.selected;
    upgrade.selected = SMR_UPGRADE_NONE;
    editorapp::Tick();
    CHECK(!modalShown && !editorapp::shopOpen && automaticReady);
    automaticReady = false;
    due = 2750;
    pickerOpen = true;
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
    Draw();
    PressModalButton("##Orbital Upgrade", true);
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(upgrade.purchased == SMR_UPGRADE_SUPPORT && !upgrade.selected);
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
    checkoutComplete = true;
    due = 0;
    editorapp::Tick();
    CHECK(!modalShown);
    CHECK(automaticReady);
    due = 2750;
    requested = true;
    editorapp::Tick();
    CHECK(modalShown);
    pickerOpen = false;
    editorapp::Tick();
    CHECK(!modalShown);
    CHECK(!editorapp::NoticeShown());
    CHECK(!automaticReady);
    ImGui::DestroyContext();
    puts("PASS frontend: hover fades and sliding underline, live costs, reversible panels, checkout, and F2 exchange.");
}
