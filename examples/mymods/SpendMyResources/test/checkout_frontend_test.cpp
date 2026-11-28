#include "../src/shop/frontend/frontend_internal.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdarg.h>
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
static size_t exchangeFrom, exchangeTo;
static unsigned exchangeBatches;
static unsigned exchangeAmounts[CURRENCY_COUNT] = {5000, 10, 10, 10, 10};
static unsigned exchangeCaps[CURRENCY_COUNT] = {50000, 500, 500, 500, 500};
static bool exchangeReady = true, exchangePending;
static bool checkoutComplete;
static int purchaseCalls, continueCalls;
static std::string fixtureLog;
static SmrDestroyerStatus upgrade = {{5000, 40, 20, 3, 50}, 1, SMR_UPGRADE_NONE, SMR_UPGRADE_NONE, 0};
static SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT] = {
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
static void CaptureLog(HfMod*, const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    fixtureLog += text;
    fixtureLog += '\n';
}
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
static SmrEquipmentStatus equipment = {{}, {}, 1};
extern "C" SmrEquipmentStatus SmrEquipment(void) { return equipment; }
extern "C" int SmrSelectEquipment(HfMod*, const SmrEquipmentGroup* group, unsigned choice)
{
    equipment.tiers[group->tier].selected = choice;
    return 1;
}
static SmrHelldiverStatus helldiver = {{5000, 40, 20, 3, 50}, {1, 1, 1}};
static const SmrHelldiverTuning helldiverTuning[SMR_PERK_COUNT] = {
    {},
    {{0, 15}, -50, 50},
    {{0, 15}, 50, -50},
    {{0, 15}, 33, 33, -50, -10},
    {{0, 0, 10}, -10, -10, 0, 25},
    {{0, 0, 10}, 0, 0, 0, 0, 10, -25},
    {{0, 0, 10}, 0, 0, 0, 0, -10, 25},
    {{0, 0, 0, 5}, 0, 0, 0, -10, 0, 0, 0, 20, 30},
    {{0, 0, 0, 5}, 0, 0, 0, -10, 0, 0, -10},
};
static bool helldiverConfigAvailable = true;
extern "C" SmrHelldiverStatus SmrHelldiver(HfMod*) { return helldiver; }
extern "C" int SmrSelectHelldiver(HfMod*, SmrHelldiverTier tier, SmrHelldiverPerk perk)
{
    helldiver.selected[tier] = perk;
    return 1;
}
extern "C" const SmrHelldiverTuning* SmrGetHelldiverTuning(SmrHelldiverPerk perk)
{
    return helldiverConfigAvailable && perk > SMR_PERK_NONE && perk < SMR_PERK_COUNT ? &helldiverTuning[perk] : nullptr;
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
static SmrKillNotice killNotice;
static SmrKillSummary killSummary;
static ULONGLONG fixtureNow = 1000;
static ULONGLONG WINAPI FixtureClock() { return fixtureNow; }
extern "C" void SmrLastKill(SmrKillNotice* out) { *out = killNotice; }
extern "C" void SmrLastKillSummary(SmrKillSummary* out) { *out = killSummary; }
extern "C" SmrCheckoutAction SmrCheckout(unsigned* total, unsigned upgrade_price[CURRENCY_COUNT], int* pending)
{
    *total = due;
    memcpy(upgrade_price, tuning[upgrade.selected].price, sizeof tuning[0].price);
    *pending = transactionPending || checkoutComplete;
    if (*pending || SmrRequisitionDue(due, upgrade_price) > upgrade.balance[REQUISITION_SLIPS])
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
    *item = {names[i], points[i], exchangeAmounts[i], exchangeCaps[i], exchangeReady, exchangePending};
    return 1;
}
extern "C" int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit)
{
    *debit = 50 * batches; *credit = batches;
    return from != to && batches;
}
extern "C" int SmrExchange(HfMod*, size_t from, size_t to, unsigned batches)
{
    exchangeFrom = from;
    exchangeTo = to;
    exchangeBatches = batches;
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

static void CheckCostPanel(const char* total, bool breakdown, float width = 1280, float height = 720)
{
    CHECK(editorapp::NoticeShown());
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        editorapp::Overlay(nullptr, width, height);
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
    /* The default spot, independent of the display size. */
    CHECK(panel->Pos.x == 577 && panel->Pos.y == 88);
    CHECK(!modalShown && !transactionPending && !upgrade.selected && !automaticReady);
    ImGui::GetIO().DisplaySize = display;
}

static void CheckCustomCostPosition()
{
    ImGuiWindow* panel = ImGui::FindWindowByName("Loadout cost");
    CHECK(panel);
    auto& position = editorapp::panelPositions[editorapp::PositionIndex(editorapp::PositionPanel::LoadoutCost)];
    position.position = ImVec2(180, 90);
    position.hasPosition = true;
    editorapp::SavePanelPositions();
    position = {};
    editorapp::panelPositionsLoaded = false;
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        editorapp::Overlay(nullptr, 1280, 720);
        ImGui::Render();
        CHECK(panel->Pos.x == 180 && panel->Pos.y == 90 && (panel->Flags & ImGuiWindowFlags_NoInputs));
    }
}

static std::string DrawKillCard()
{
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    editorapp::Overlay(nullptr, 1280, 720);
    std::string text = GImGui->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::Render();
    return text;
}

static void CheckKillCard()
{
    editorapp::killClock = FixtureClock;
    fixtureState = HF_STATE_MISSION;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    killNotice = {1, 10000, 10000, 10300};
    editorapp::Tick();
    CHECK(editorapp::NoticeShown() && editorapp::KillCardAlpha(fixtureNow) == 0);
    std::string text = DrawKillCard();
    CHECK(text.find("Kill reward") != std::string::npos && text.find("0") != std::string::npos &&
        text.find("10000") == std::string::npos && text.find("Req") == std::string::npos);
    ImGuiWindow* card = ImGui::FindWindowByName("Kill reward");
    /* The default spot (1221, 27), moved left so the whole card fits the 1280-wide screen. */
    CHECK(card && (card->Flags & ImGuiWindowFlags_NoInputs) && card->Pos.x == 1060 && card->Pos.y == 27);
    fixtureNow += 350;
    const double halfway = editorapp::KillCardAmount(fixtureNow);
    CHECK(halfway > 0 && halfway < 300 && editorapp::KillCardAlpha(fixtureNow) == 1);
    DrawKillCard();
    float titleLeft = FLT_MAX, titleRight = -FLT_MAX;
    for (const ImDrawVert& vertex : card->DrawList->VtxBuffer)
        if (vertex.col == editorapp::kAccentText)
        {
            titleLeft = std::min(titleLeft, vertex.pos.x);
            titleRight = std::max(titleRight, vertex.pos.x);
        }
    CHECK(titleLeft < titleRight &&
        std::abs((titleLeft + titleRight) / 2 - (card->Pos.x + card->Size.x / 2)) <= 2);

    /* A changing wallet must not disturb the displayed reward when another kill arrives. */
    killNotice = {2, 20000, 20300, 20600};
    editorapp::Tick();
    CHECK(editorapp::KillCardAmount(fixtureNow) == halfway && editorapp::KillCardAlpha(fixtureNow) == 1);
    fixtureNow += 700;
    text = DrawKillCard();
    CHECK(text.find("600") != std::string::npos && text.find("20600") == std::string::npos &&
        text.find("20000") == std::string::npos && text.find("Req") == std::string::npos);
    fixtureNow += 4300;
    CHECK(editorapp::NoticeShown() && editorapp::KillCardAlpha(fixtureNow) == 1);
    fixtureNow += 250;
    CHECK(editorapp::KillCardAlpha(fixtureNow) == 0.5f);
    fixtureNow += 250;
    CHECK(!editorapp::NoticeShown() && DrawKillCard().find("Kill reward") == std::string::npos);

    killNotice = {3, 49900, 49950, 50000};
    editorapp::Tick();
    CHECK(editorapp::KillCardAmount(fixtureNow) == 50);
    fixtureNow += 700;
    CHECK(editorapp::KillCardAmount(fixtureNow) == 100 && DrawKillCard().find("100") != std::string::npos);
    fixtureNow += 4800;
    CHECK(!editorapp::NoticeShown());
    fixtureState = HF_STATE_SHIP;
}

static void CheckKillSummary()
{
    fixtureState = HF_STATE_MISSION;
    killNotice = {4, 10000, 10000, 10327};
    editorapp::Tick();
    CHECK(editorapp::NoticeShown());
    killSummary = {1, 3, 2, 2, 601, 501, 301, 26, 327, 0};
    editorapp::Tick();
    fixtureState = HF_STATE_PREPARE_MISSION;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureNow += 9000;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureState = HF_STATE_SHIP;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureNow += 2999;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    ++fixtureNow;
    editorapp::Tick();
    const ULONGLONG shownAt = fixtureNow;
    CHECK(editorapp::KillSummaryShowing(fixtureNow) && editorapp::KillCardAlpha(fixtureNow) == 0);
    fixtureNow += 250;
    editorapp::Tick();
    CHECK(editorapp::KillCardAlpha(fixtureNow) == 1 && editorapp::KillCardAmount(fixtureNow) == 327);
    pickerOpen = true;
    editorapp::Tick();
    const std::string text = DrawKillCard();
    CHECK(text.find("Mission kill rewards") != std::string::npos &&
        text.find("327") != std::string::npos &&
        text.find("Team kills: 3 (601 Req)") != std::string::npos &&
        text.find("Your kills: 2 (501 Req)") != std::string::npos &&
        text.find("Your share (2 players): 301 Req") != std::string::npos &&
        text.find("Contribution bonus: 26 Req") != std::string::npos &&
        text.find("Earned reward before the wallet cap.") != std::string::npos &&
        text.find("abandoned") == std::string::npos &&
        text.find("Loadout cost") == std::string::npos);
    pickerOpen = false;
    editorapp::Tick();
    fixtureNow = shownAt + 10000;
    editorapp::Tick();
    CHECK(editorapp::NoticeShown() && editorapp::KillCardAlpha(fixtureNow) == 1);
    fixtureNow += 250;
    CHECK(editorapp::KillCardAlpha(fixtureNow) == 0.5f);
    fixtureNow += 250;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureNow += 10000;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());

    fixtureState = HF_STATE_MISSION;
    editorapp::Tick();
    killSummary = {2, 0, 0, 1, 0, 0, 0, 0};
    editorapp::Tick();
    fixtureState = HF_STATE_SHIP;
    editorapp::Tick();
    fixtureNow += 3000;
    editorapp::Tick();
    CHECK(editorapp::KillSummaryShowing(fixtureNow) && editorapp::KillCardAmount(fixtureNow) == 0);
    fixtureNow += 250;
    editorapp::Tick();
    CHECK(DrawKillCard().find("Your share (1 player): 0 Req") != std::string::npos);
    fixtureState = HF_STATE_PREPARE_MISSION;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureState = HF_STATE_SHIP;
    editorapp::Tick();
    fixtureNow += 5000;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());

    /* Settlement can arrive after the ship transition; its delay still starts on returning to the ship. */
    killSummary = {3, 1, 1, 1, 500, 500, 500, 0, 100, 1};
    editorapp::Tick();
    CHECK(editorapp::KillSummaryShowing(fixtureNow) && editorapp::KillCardAmount(fixtureNow) == 100);
    fixtureNow += 250;
    CHECK(DrawKillCard().find("Mission abandoned: 20% paid") != std::string::npos);
    fixtureState = HF_STATE_MISSION;
    editorapp::Tick();
    killSummary = {4, 1, 1, 1, 100, 100, 100, 0, 100, 0};
    editorapp::Tick();
    fixtureState = HF_STATE_SHIP;
    editorapp::Tick();
    fixtureNow += 2999;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
    fixtureState = HF_STATE_MISSION;
    editorapp::Tick();
    fixtureState = HF_STATE_SHIP;
    editorapp::Tick();
    fixtureNow += 5000;
    editorapp::Tick();
    CHECK(!editorapp::NoticeShown());
}

static bool f3Down;
static bool FixtureF3() { return f3Down; }

static void PressF3()
{
    f3Down = true;
    editorapp::Tick();
    f3Down = false;
    editorapp::Tick();
}

static void DrawLayout()
{
    ImGui::NewFrame();
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
}

static void CheckLayoutScreen()
{
    using editorapp::PositionIndex;
    using editorapp::PositionPanel;
    auto& positions = editorapp::panelPositions;
    editorapp::layoutKeyDown = FixtureF3;
    /* F3 covers an open checkout, which waits hidden and returns when the layout screen closes. */
    pickerOpen = requested = true;
    editorapp::Tick();
    CHECK(editorapp::shopOpen && modalShown);
    PressF3();
    CHECK(editorapp::layoutOpen && editorapp::shopOpen && modalShown);
    DrawLayout();
    CHECK(ImGui::FindWindowByName("###layout-checkout")->Active &&
        !ImGui::FindWindowByName("Spend My Resources")->Active);
    checkoutComplete = true; /* a purchase finishing under the screen readies only after it closes */
    editorapp::Tick();
    CHECK(editorapp::shopOpen && !automaticReady);
    PressF3();
    CHECK(!editorapp::layoutOpen && !editorapp::shopOpen && !modalShown && automaticReady);
    requested = true;
    editorapp::Tick();
    PressF3();
    DrawLayout();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    DrawLayout();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    CHECK(!editorapp::layoutOpen && editorapp::shopOpen && modalShown);
    DrawLayout();
    CHECK(editorapp::shopOpen && ImGui::FindWindowByName("Spend My Resources")->Active);
    pickerOpen = false;
    editorapp::Tick();
    CHECK(!editorapp::shopOpen && !modalShown);
    const ImVec2 cost = positions[PositionIndex(PositionPanel::LoadoutCost)].position;
    CHECK(!positions[PositionIndex(PositionPanel::Checkout)].hasPosition &&
        !positions[PositionIndex(PositionPanel::Exchange)].hasPosition &&
        !positions[PositionIndex(PositionPanel::KillReward)].hasPosition && cost.x == 180 && cost.y == 90);

    PressF3();
    CHECK(editorapp::layoutOpen && modalShown && !editorapp::NoticeShown());
    DrawLayout();
    DrawLayout();
    const char* previews[] = {"###layout-checkout", "###layout-exchange", "###layout-loadout-cost", "###layout-kill-reward"};
    for (const char* name : previews)
    {
        const ImGuiWindow* preview = ImGui::FindWindowByName(name);
        CHECK(preview && preview->Active &&
            !(preview->Flags & (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoMouseInputs)));
    }
    const ImGuiWindow* killPreview = ImGui::FindWindowByName("###layout-kill-reward");
    CHECK(killPreview->Pos.x == 1060 && killPreview->Pos.y == 27); /* its default, kept fully on screen */

    /* Drag the exchange preview by its body. */
    ImGuiWindow* exchange = ImGui::FindWindowByName("###layout-exchange");
    const ImVec2 from = exchange->Pos;
    const ImVec2 grab(from.x + 24, from.y + exchange->Size.y - 12);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(grab.x, grab.y);
    DrawLayout();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    DrawLayout();
    io.AddMousePosEvent(grab.x - 200, grab.y + 60);
    DrawLayout();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    DrawLayout();
    CHECK(exchange->Pos.x == from.x - 200 && exchange->Pos.y == from.y + 60);

    io.AddKeyEvent(ImGuiKey_Escape, true);
    DrawLayout();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    CHECK(!editorapp::layoutOpen && !modalShown);
    for (auto& position : positions) position = {};
    editorapp::panelPositionsLoaded = false;
    editorapp::LoadPanelPositions();
    const auto& saved = positions[PositionIndex(PositionPanel::Exchange)];
    CHECK(saved.hasPosition && saved.position.x == from.x - 200 && saved.position.y == from.y + 60);
    /* Panels left at their default spot keep it. */
    CHECK(!positions[PositionIndex(PositionPanel::Checkout)].hasPosition &&
        !positions[PositionIndex(PositionPanel::KillReward)].hasPosition &&
        positions[PositionIndex(PositionPanel::LoadoutCost)].position.x == 180);

    /* The real exchange opens where its preview was left, and F3 closes the layout screen too. */
    editorapp::Opened();
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        editorapp::Frame(nullptr, 1280, 720, 1.0f / 60);
        ImGui::Render();
    }
    const ImGuiWindow* real = ImGui::FindWindowByName("Spend My Resources - Currency Exchange");
    CHECK(real->Pos.x == saved.position.x && real->Pos.y == saved.position.y);
    windowClosed = false;
    PressF3();
    CHECK(windowClosed && !editorapp::exchangeOpen && editorapp::layoutOpen && modalShown);
    DrawLayout();
    PressF3();
    CHECK(!editorapp::layoutOpen && !modalShown);
}

static void STDMETHODCALLTYPE ExecuteTestLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    queue->ExecuteCommandLists(count, lists);
}

static int CheckCurrencyImages(const char* root, bool d3d12)
{
    fixtureApi.log = CaptureLog;
    editorapp::Host host = {};
    host.api = &fixtureApi;
    host.modFolder = std::string(root) + "\\valid";
    editorapp::Init(host);
    for (const char* file : editorapp::kCurrencyIcons)
        CHECK(icons::Find(icons::MOD, file));
    CHECK(fixtureLog.find("Currency icon") == std::string::npos);

    fixtureLog.clear();
    host.modFolder = std::string(root) + "\\partial";
    editorapp::Init(host);
    CHECK(!icons::Find(icons::MOD, editorapp::kCurrencyIcons[COMMON_SAMPLES]));
    CHECK(!icons::Find(icons::MOD, editorapp::kCurrencyIcons[RARE_SAMPLES]));
    CHECK(icons::Find(icons::MOD, editorapp::kCurrencyIcons[REQUISITION_SLIPS]));
    CHECK(icons::Find(icons::MOD, editorapp::kCurrencyIcons[SUPER_SAMPLES]));
    CHECK(icons::Find(icons::MOD, editorapp::kCurrencyIcons[FORGE_CREDITS]));
    CHECK(fixtureLog.find("Currency icon Common_Sample_Icon.png is missing or unreadable") != std::string::npos);
    CHECK(fixtureLog.find("Currency icon Rare_Sample_Icon.png is missing or unreadable") != std::string::npos);

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.0f / 60;
    unsigned char* fontPixels;
    int fontWidth, fontHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D12Device* device12 = nullptr;
    ID3D12CommandQueue* queue12 = nullptr;
    ID3D12DescriptorHeap* heap12 = nullptr;
    if (d3d12)
    {
        CHECK(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12))));
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CHECK(SUCCEEDED(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue12))));
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CHECK(SUCCEEDED(device12->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap12))));
        CHECK(icons::Upload(device12, queue12, ExecuteTestLists,
            heap12->GetCPUDescriptorHandleForHeapStart(), heap12->GetGPUDescriptorHandleForHeapStart()));
    }
    else
    {
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)));
        CHECK(icons::Upload11(device));
        auto* view = reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<intptr_t>(icons::Texture()));
        ID3D11Resource* resource = nullptr;
        view->GetResource(&resource);
        ID3D11Texture2D* atlas = nullptr;
        CHECK(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&atlas))));
        resource->Release();
        D3D11_TEXTURE2D_DESC desc = {};
        atlas->GetDesc(&desc);
        desc.MipLevels = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* readback = nullptr;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)));
        context->CopySubresourceRegion(readback, 0, 0, 0, 0, atlas, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        CHECK(SUCCEEDED(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped)));
        const icons::Icon* icon = icons::Find(icons::MOD, editorapp::kCurrencyIcons[REQUISITION_SLIPS]);
        CHECK(icon && icon->width == 128 && icon->height == 128);
        const unsigned left = static_cast<unsigned>(icon->uv0.x * desc.Width + 0.5f);
        const unsigned top = static_cast<unsigned>(icon->uv0.y * desc.Height + 0.5f);
        const auto pixel = [&](unsigned x, unsigned y) {
            return static_cast<const uint8_t*>(mapped.pData) + (top + y) * mapped.RowPitch + (left + x) * 4;
        };
        const uint8_t transparent[] = {0, 0, 0, 0}, red[] = {255, 0, 0, 255}, blue[] = {0, 0, 255, 255};
        CHECK(!memcmp(pixel(8, 8), transparent, 4));
        CHECK(!memcmp(pixel(32, 32), red, 4));
        CHECK(!memcmp(pixel(56, 56), blue, 4));
        CHECK(!memcmp(pixel(72, 32), transparent, 4));
        CHECK(!memcmp(pixel(100, 100), transparent, 4));
        context->Unmap(readback, 0);
        readback->Release();
        atlas->Release();
    }

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(300, 300));
    ImGui::Begin("Currency icon fixture");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        const icons::Icon* expected = icons::Find(icons::MOD, editorapp::kCurrencyIcons[i]);
        const int first = draw->VtxBuffer.Size;
        editorapp::DrawCurrencyIcon(i);
        if (expected)
        {
            CHECK(draw->VtxBuffer.Size == first + 4);
            CHECK(draw->VtxBuffer[first].uv.x == expected->uv0.x && draw->VtxBuffer[first].uv.y == expected->uv0.y);
            CHECK(draw->VtxBuffer[first + 2].uv.x == expected->uv1.x && draw->VtxBuffer[first + 2].uv.y == expected->uv1.y);
        }
        else CHECK(draw->VtxBuffer.Size > first + 4);
        ImGui::NewLine();
    }
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    if (context) context->Release();
    if (device) device->Release();
    if (heap12) heap12->Release();
    if (queue12) queue12->Release();
    if (device12) device12->Release();
    printf("Currency SVG and icon fixtures passed: D3D%d.\n", d3d12 ? 12 : 11);
    return 0;
}

static int CurrencyIndexForUv(const ImVec2& uv)
{
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        const icons::Icon* icon = icons::Find(icons::MOD, editorapp::kCurrencyIcons[i]);
        if (icon && uv.x >= icon->uv0.x && uv.x <= icon->uv1.x && uv.y >= icon->uv0.y && uv.y <= icon->uv1.y)
            return static_cast<int>(i);
    }
    return -1;
}

static void SaveFrontendScreenshot(const char* path, bool expectGameIcons, bool exchange = false)
{
    const UINT width = static_cast<UINT>(ImGui::GetIO().DisplaySize.x);
    const UINT height = static_cast<UINT>(ImGui::GetIO().DisplaySize.y);
    static struct ScreenshotDevice {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        ScreenshotDevice()
        {
            CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &device, nullptr, &context)));
        }
        ~ScreenshotDevice() { context->Release(); device->Release(); }
    } renderer;
    ID3D11Device* device = renderer.device;
    ID3D11DeviceContext* context = renderer.context;
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
    if (exchange)
    {
        ImGui::NewFrame();
        editorapp::Frame(nullptr, static_cast<float>(width), static_cast<float>(height), 1.0f / 60);
        ImGui::Render();
    }
    else Draw();
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    unsigned atlasIndices = 0, currencyIndices = 0, currencyByType[CURRENCY_COUNT] = {};
    const ImDrawData& data = *ImGui::GetDrawData();
    for (const ImDrawList* list : data.CmdLists)
        for (const ImDrawCmd& command : list->CmdBuffer)
            if (command.GetTexID() == icons::Texture())
            {
                atlasIndices += command.ElemCount;
                for (unsigned i = 0; i < command.ElemCount; ++i)
                {
                    unsigned vertex = list->IdxBuffer[command.IdxOffset + i] + command.VtxOffset;
                    CHECK(vertex < static_cast<unsigned>(list->VtxBuffer.Size));
                    const int currency = CurrencyIndexForUv(list->VtxBuffer[vertex].uv);
                    if (currency >= 0) { ++currencyIndices; ++currencyByType[currency]; }
                }
            }
    if (exchange)
    {
        for (size_t i = 0; i < CURRENCY_COUNT; ++i)
            if (icons::Find(icons::MOD, editorapp::kCurrencyIcons[i])) CHECK(currencyByType[i] >= 24);
    }
    else
    {
        const unsigned expectedIcons = editorapp::checkoutPanel == editorapp::CheckoutPanel::Helldiver ? 7 : (4 + 4) * 2;
        const unsigned expectedGameIndices = expectGameIcons ? expectedIcons * 6u : 0;
        if (atlasIndices - currencyIndices != expectedGameIndices)
            printf("Game icon atlas indices: %u (expected %u)\n", atlasIndices - currencyIndices, expectedGameIndices);
        CHECK(atlasIndices - currencyIndices == expectedGameIndices);
        if (editorapp::checkoutPanel == editorapp::CheckoutPanel::Destroyer)
            for (size_t i : editorapp::kUpgradeCurrencies)
                if (icons::Find(icons::MOD, editorapp::kCurrencyIcons[i])) CHECK(currencyByType[i] >= 6);
        if (editorapp::checkoutPanel == editorapp::CheckoutPanel::Helldiver &&
            icons::Find(icons::MOD, editorapp::kCurrencyIcons[SUPER_SAMPLES]))
            CHECK(currencyByType[SUPER_SAMPLES] >= 6);
    }
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
    printf("Saving frontend screenshot: %s\n", path);
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

static ImRect PressModalButton(const char* label, bool inPanel = false, int tier = -1)
{
    ImGuiWindow* window = inPanel ? PanelWindow() : CheckoutWindow();
    ImGui::GetIO().DeltaTime = 1.0f / 60;
    ImGui::NewFrame();
    GImGui->NavWindow = window;
    GImGui->NavId = GImGui->NavActivateId = GImGui->NavActivateDownId =
        tier < 0 ? window->GetID(label) : ImHashStr(label, 0, window->GetID(tier));
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
    return window->NavRectRel[ImGuiNavLayer_Main];
}

static ImRect PressExchangeButton(const char* label, const char* scope = nullptr)
{
    ImGuiWindow* window = ImGui::FindWindowByName("Spend My Resources - Currency Exchange");
    CHECK(window);
    ImGui::NewFrame();
    const ImGuiID id = scope ? ImHashStr(label, 0, window->GetID(scope)) : window->GetID(label);
    GImGui->NavWindow = window;
    GImGui->NavId = GImGui->NavActivateId = GImGui->NavActivateDownId = id;
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Frame(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
    return window->NavRectRel[ImGuiNavLayer_Main];
}

static void CheckExchangeText(const char* first, const char* second = nullptr, const char* third = nullptr)
{
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    editorapp::Frame(nullptr, 1280, 720, 1.0f / 60);
    for (const char* expected : {first, second, third})
    {
        if (!expected) continue;
        if (!strstr(GImGui->LogBuffer.c_str(), expected))
            printf("Missing exchange text: %s\nDisplayed:\n%s\n", expected, GImGui->LogBuffer.c_str());
        CHECK(strstr(GImGui->LogBuffer.c_str(), expected));
    }
    ImGui::LogFinish();
    ImGui::Render();
}

static void CheckExchangeControls(const char* screenshot)
{
    ImGuiWindow* window = ImGui::FindWindowByName("Spend My Resources - Currency Exchange");
    CHECK(window);
    CheckExchangeText("Give: Requisition slips", "Receive: Common samples", "One batch:");
    if (screenshot)
    {
        GImGui->NavId = 0;
        const std::string path = std::string(screenshot) + ".exchange.bmp";
        SaveFrontendScreenshot(path.c_str(), false, true);
    }
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 1 && exchangeFrom == REQUISITION_SLIPS &&
        exchangeTo == COMMON_SAMPLES && exchangeBatches == 1);

    ImRect give[CURRENCY_COUNT], receive[CURRENCY_COUNT];
    for (int i = 0; i < static_cast<int>(CURRENCY_COUNT); ++i)
    {
        char label[32];
        snprintf(label, sizeof label, "##exchange-give-%d", i);
        give[i] = PressExchangeButton(label);
    }
    for (int i = 0; i < static_cast<int>(CURRENCY_COUNT); ++i)
    {
        char label[32];
        snprintf(label, sizeof label, "##exchange-receive-%d", i);
        receive[i] = PressExchangeButton(label);
        CHECK(receive[i].Min.x == give[i].Min.x && receive[i].Min.y > give[i].Max.y);
        if (i)
        {
            CHECK(give[i].Min.y == give[0].Min.y && give[i - 1].Max.x < give[i].Min.x);
            CHECK(receive[i].Min.y == receive[0].Min.y && receive[i - 1].Max.x < receive[i].Min.x);
        }
    }
    const float arrowX = (give[0].Min.x + give[CURRENCY_COUNT - 1].Max.x) / 2 + window->Pos.x;
    const float arrowTop = give[0].Max.y + window->Pos.y, arrowBottom = receive[0].Min.y + window->Pos.y;
    bool arrowDrawn = false;
    for (const ImDrawVert& vertex : window->DrawList->VtxBuffer)
        if (vertex.col == editorapp::kAccent && std::abs(vertex.pos.x - arrowX) <= 7 &&
            vertex.pos.y > arrowTop && vertex.pos.y < arrowBottom)
            arrowDrawn = true;
    CHECK(arrowDrawn);
    CheckExchangeText("Give: Forge Credits", "Receive: Forge Credits", "Choose different currencies");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 1);

    exchangeAmounts[COMMON_SAMPLES] = 100;
    PressExchangeButton("##exchange-give-1");
    CheckExchangeText("Give: Common samples", "Receive: Forge Credits", "One batch:");
    PressExchangeButton("+", "Batches");
    CheckExchangeText("Give 100 Receive 2");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 2 && exchangeFrom == COMMON_SAMPLES && exchangeTo == FORGE_CREDITS && exchangeBatches == 2);

    exchangePending = true;
    PressExchangeButton("##exchange-give-0");
    PressExchangeButton("##exchange-receive-3");
    PressExchangeButton("Processing...");
    CHECK(exchangeCalls == 2);
    exchangePending = false;
    CheckExchangeText("Give: Common samples", "Receive: Forge Credits");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 3 && exchangeFrom == COMMON_SAMPLES && exchangeTo == FORGE_CREDITS && exchangeBatches == 2);

    exchangeAmounts[COMMON_SAMPLES] = 49;
    CheckExchangeText("Insufficient source currency.");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 3);
    exchangeAmounts[COMMON_SAMPLES] = 100;
    exchangeAmounts[FORGE_CREDITS] = exchangeCaps[FORGE_CREDITS];
    CheckExchangeText("This exchange would exceed the destination cap.");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 3);
    exchangeAmounts[FORGE_CREDITS] = 10;
    exchangeReady = false;
    CheckExchangeText("unavailable");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 3);
    exchangeReady = true;
    PressExchangeButton("-", "Batches");
    PressExchangeButton("-", "Batches");
    CheckExchangeText("Choose different currencies and a positive, valid batch count.");
    PressExchangeButton("Exchange");
    CHECK(exchangeCalls == 3);
    PressExchangeButton("+", "Batches");
    editorapp::Closed();
    editorapp::Opened();
    CheckExchangeText("Give: Common samples", "Receive: Forge Credits");
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
    ImGuiWindow* description = panel->DC.ChildWindows[0];
    CHECK(description->DC.ChildWindows.Size == 1);
    ImGuiWindow* grid = description->DC.ChildWindows[0];
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

static ImRect PressHelldiverPerk(SmrHelldiverTier tier, SmrHelldiverPerk perk)
{
    ImGuiWindow* panel = PanelWindow();
    const ImGuiID tierId = ImHashData(&tier, sizeof(int), panel->GetID("Helldiver tiers"));
    const int perkIndex = perk;
    const ImGuiID id = perk == SMR_PERK_NONE ? ImHashStr(" X ##Clear helldiver", 0, tierId) :
        ImHashStr("##Perk", 0, ImHashData(&perkIndex, sizeof perkIndex, tierId));
    ImGui::NewFrame();
    GImGui->NavWindow = panel;
    GImGui->NavId = GImGui->NavActivateId = GImGui->NavActivateDownId = id;
    GImGui->NavInputSource = ImGuiInputSource_Keyboard;
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    ImGui::Render();
    ImRect rect = panel->NavRectRel[ImGuiNavLayer_Main];
    rect.Translate(panel->Pos);
    return rect;
}

static void CheckModalText(const char* text, const char* second = nullptr, const char* third = nullptr);

static void CheckHelldiverTab(const char* screenshot, bool expectGameIcons)
{
    CHECK(strcmp(editorapp::kPerkIcons[SMR_PERK_PRIMARY_TRADEOFF].sprite, "P-2 Peacemaker") == 0 &&
        strcmp(editorapp::kPerkIcons[SMR_PERK_SECONDARY_TRADEOFF].sprite, "AR-23 Liberator") == 0);
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
    const ImRect primary = PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF);
    const ImRect secondary = PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_SECONDARY_TRADEOFF);
    const ImRect grenade = PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_GRENADE_KIT);
    CHECK(primary.Min.y == secondary.Min.y && secondary.Min.y == grenade.Min.y &&
        primary.Max.y == secondary.Max.y && secondary.Max.y == grenade.Max.y);
    const ImRect armored = PressHelldiverPerk(SMR_TIER_ELITE, SMR_PERK_ARMORED);
    const ImRect medic = PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_FIELD_MEDIC);
    CHECK(helldiver.selected[SMR_TIER_PLUS] == SMR_PERK_GRENADE_KIT && helldiver.selected[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC &&
        helldiver.selected[SMR_TIER_ELITE] == SMR_PERK_ARMORED);
    CHECK(grenade.Min.x == armored.Min.x + 2 * (grenade.GetWidth() + ImGui::GetStyle().ItemSpacing.x));
    CHECK(armored.Min.x == medic.Min.x - grenade.GetWidth() - ImGui::GetStyle().ItemSpacing.x);
    CHECK(grenade.Max.y < armored.Min.y && armored.Max.y < medic.Min.y);
    ImGuiWindow* panel = PanelWindow();
    CHECK(panel->DC.ChildWindows.Size == 1);
    ImGuiWindow* description = panel->DC.ChildWindows[0];
    Draw();
    CHECK(description->Pos.y > medic.Max.y && panel->ScrollMax.y == 0);
    CheckModalText("Super: Field Medic", "Regenerate 1 HP every second.", "Armor rating: -10%");
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(grenade.GetCenter().x, grenade.GetCenter().y);
    CheckModalText("Plus: Grenade Kit", "Throwables: -50%", "Cost:");
    CHECK(helldiver.selected[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC &&
        editorapp::helldiverSelection == SMR_PERK_FIELD_MEDIC);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    CheckModalText("Super: Field Medic", "Movement speed: -10%");
    if (screenshot)
    {
        GImGui->NavId = 0;
        const std::string path = std::string(screenshot) + ".helldiver.bmp";
        SaveFrontendScreenshot(path.c_str(), expectGameIcons);
    }
    PressModalButton("Review purchase");
    CHECK(editorapp::checkoutPanel == editorapp::CheckoutPanel::Purchase && !purchaseCalls);
    CheckModalText("Plus: Grenade Kit", "Elite: Armored", "Super: Field Medic");
    PressModalButton("Helldiver");
    CheckModalText("Super: Field Medic");
    PressHelldiverPerk(SMR_TIER_ELITE, SMR_PERK_NONE);
    CHECK(!helldiver.selected[SMR_TIER_ELITE] && helldiver.selected[SMR_TIER_PLUS] == SMR_PERK_GRENADE_KIT &&
        helldiver.selected[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC);
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF);
    CHECK(helldiver.selected[SMR_TIER_PLUS] == SMR_PERK_PRIMARY_TRADEOFF);
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF);
    CHECK(!helldiver.selected[SMR_TIER_PLUS]);
    transactionPending = 1;
    PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_AUTO_STIM);
    PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_NONE);
    CHECK(helldiver.selected[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC);
    transactionPending = 0;
    helldiver.available[SMR_TIER_PLUS] = 0;
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_GRENADE_KIT);
    CHECK(!helldiver.selected[SMR_TIER_PLUS]);
    io.AddMousePosEvent(grenade.GetCenter().x, grenade.GetCenter().y);
    CheckModalText("Plus: Grenade Kit", "Upgrade selection unavailable.");
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    helldiver.available[SMR_TIER_PLUS] = 1;
    helldiver.purchased[SMR_TIER_SUPER] = SMR_PERK_FIELD_MEDIC;
    PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_AUTO_STIM);
    PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_NONE);
    CHECK(helldiver.selected[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC &&
        helldiver.purchased[SMR_TIER_SUPER] == SMR_PERK_FIELD_MEDIC);
    io.AddMousePosEvent(medic.GetCenter().x, medic.GetCenter().y);
    CheckModalText("Field Medic already purchased for your next mission.");
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    helldiver.purchased[SMR_TIER_SUPER] = SMR_PERK_NONE;
    helldiverConfigAvailable = false;
    PressHelldiverPerk(SMR_TIER_PLUS, SMR_PERK_PRIMARY_TRADEOFF);
    CHECK(!helldiver.selected[SMR_TIER_PLUS]);
    CheckModalText("Helldiver upgrades disabled: check helldiver-upgrades.json and the mod log.");
    helldiverConfigAvailable = true;
    PressHelldiverPerk(SMR_TIER_SUPER, SMR_PERK_NONE);
    CHECK(!helldiver.selected[SMR_TIER_SUPER] && editorapp::checkoutPanel == editorapp::CheckoutPanel::Helldiver);
    PressModalButton("Destroyer");
    Draw(0.20f);
    ImRect eagle = PressModalButton("##Eagle Upgrade", true);
    eagle.Translate(PanelWindow()->Pos);
    PressModalButton("##Support Upgrade", true);
    io.AddMousePosEvent(eagle.GetCenter().x, eagle.GetCenter().y);
    CheckModalText("Eagle rearm: 30 s. Call-in time: +2 s. Cooldown: 20 s.");
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT && editorapp::destroyerSelection == 3);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    CheckModalText("Equipped support stratagems: -62.5% to cooldowns and unlimited uses.");
    PressModalButton(" X ##Clear destroyer", true);
}

static void CheckModalText(const char* text, const char* second, const char* third)
{
    Draw();
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    for (const char* expected : {text, second, third})
    {
        if (!expected) continue;
        if (!strstr(GImGui->LogBuffer.c_str(), expected))
            printf("Missing text: %s\nDisplayed:\n%s\n", expected, GImGui->LogBuffer.c_str());
        CHECK(strstr(GImGui->LogBuffer.c_str(), expected));
    }
    ImGui::LogFinish();
    ImGui::Render();
}

static void CheckEquipmentTab()
{
    PressModalButton("Equipment");
    CheckModalText("No upgrades available for your equipped primary.");
    static EquipmentEdit fdi[] = {{HF_DAMAGE_R4_HYENA, HF_FIELD(DamageInfo, durable_damage),
        "damage.durable_damage", "Durable damage", 654}};
    static EquipmentEdit api[] = {{HF_DAMAGE_R4_HYENA, HF_FIELD_AT(DamageInfo, armor_penetration, 0),
        "damage.ap_direct", "Direct penetration", 5}};
    static const SmrEquipmentChoice choices[] = {{"FDI", fdi, 1}, {"API", api, 1}};
    static const SmrEquipmentGroup group = {HF_ITEM_R4_HYENA, "R-4 Hyena", SMR_TIER_SUPER, choices, 2};
    static const unsigned price[CURRENCY_COUNT] = {0, 0, 0, 5};
    auto& row = equipment.tiers[SMR_TIER_SUPER];
    row = {&group, price, 0, 0, 1};
    CheckModalText("R-4 Hyena", "FDI", "API");
    CheckModalText("Durable damage: 654");
    PressModalButton("FDI", true, SMR_TIER_SUPER);
    PressModalButton("API", true, SMR_TIER_SUPER);
    CheckModalText("Selected for purchase: API", "Direct penetration: 5");
    PressModalButton("Review purchase");
    CheckModalText("R-4 Hyena - Super: API");
    PressModalButton("Equipment");
    PressModalButton("API", true, SMR_TIER_SUPER);
    CHECK(!row.selected);
    equipment.configured = 0;
    CheckModalText("Equipment upgrades disabled: check equipment-upgrades.json and the mod log.");
    PressModalButton("FDI", true, SMR_TIER_SUPER);
    CHECK(!row.selected);
    equipment = {};
    equipment.configured = 1;
    PressModalButton("Destroyer");
    Draw(0.20f);
}

int main(int argc, char** argv)
{
    if (argc == 3 && (!strcmp(argv[1], "--currency-icons") || !strcmp(argv[1], "--currency-icons12")))
        return CheckCurrencyImages(argv[2], !strcmp(argv[1], "--currency-icons12"));
    if (argc > 4)
    {
        puts("Usage: checkout_frontend_test.exe [screenshot.bmp [HellForge mods folder or - [mod folder]]]");
        return 2;
    }
    fixtureApi.game_state = FixtureState;
    editorapp::Host host = {};
    host.api = &fixtureApi; host.modal = Show; host.close = Close;
    const bool expectGameIcons = argc >= 3 && strcmp(argv[2], "-") != 0;
    if (expectGameIcons) host.modsFolder = argv[2];
    if (argc == 4) host.modFolder = argv[3];
    editorapp::Init(host);
    ImGui::CreateContext();
    wchar_t temporary[MAX_PATH];
    const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporary);
    CHECK(temporaryLength > 0 && temporaryLength < MAX_PATH);
    editorapp::panelPositionsPath = std::wstring(temporary) + L"HellForge-SpendMyResources-" +
        std::to_wstring(GetCurrentProcessId()) + L".json";
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
    CheckCostPanel("Loadout cost unavailable", false, 1920, 1080);
    CheckCustomCostPosition();
    requested = true;
    editorapp::Tick();
    CHECK(modalShown && editorapp::shopOpen && !editorapp::NoticeShown());
    Draw();
    Draw();
    CheckAnimatedTabs();
    CHECK(CheckoutWindow()->Flags & ImGuiWindowFlags_NoMove);
    CheckHelldiverTab(argc >= 2 ? argv[1] : nullptr, expectGameIcons);
    CheckEquipmentTab();
    ImGuiWindow* checkoutWindow = CheckoutWindow();
    const ImVec2 checkoutPos = checkoutWindow->Pos;
    const int opened = modalChanges;
    const char* choices[] = {"##Eagle Upgrade", "##Defensive Upgrade", "##Support Upgrade", "##Extra Stratagem Slot"};
    const SmrUpgrade expected[] = {SMR_UPGRADE_EAGLE, SMR_UPGRADE_DEFENSIVE, SMR_UPGRADE_SUPPORT, SMR_UPGRADE_NONE};
    for (size_t i = 0; i < IM_ARRAYSIZE(choices); ++i)
    {
        PressModalButton(choices[i], true);
        CHECK(editorapp::destroyerSelection == i + 1);
        CHECK(upgrade.selected == expected[i]);
        CHECK(editorapp::checkoutPanel == editorapp::CheckoutPanel::Destroyer && !purchaseCalls && modalShown);
    }
    PressModalButton("Review purchase");
    CHECK(!upgrade.selected && !purchaseCalls && modalShown);
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(!editorapp::destroyerSelection && !editorapp::destroyerExtra && !upgrade.selected);
    PressModalButton("##Extra Stratagem Slot", true);
    CheckModalText("Choose an extra stratagem", "Cost:", "Selected: None");
    CheckModalText("50 / 50", "40 / 10", "3 / 1");
    CheckModalText("20 / 5", "5000 / 2000");
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
    if (argc >= 2) SaveFrontendScreenshot(argv[1], expectGameIcons);
    const ImRect select = PressModalButton("Review purchase");
    CHECK(select.Max.y > select.Min.y && select.Max.y + checkoutPos.y <= checkoutWindow->InnerClipRect.Max.y);
    CHECK(upgrade.selected == SMR_UPGRADE_EXTRA_SLOT && upgrade.extra == HF_STRATAGEM_EAGLE_AIRSTRIKE && !purchaseCalls);
    CheckModalText("Extra stratagem: Eagle Airstrike", "Extra Stratagem Slot", "Total due:");
    CheckModalText("4750", "50", "10");
    PressModalButton("Destroyer");
    CheckModalText("Selected for purchase: Extra Stratagem Slot", "Selected: Eagle Airstrike");
    PressExtraTile(HF_STRATAGEM_EAGLE_500KG_BOMB);
    CHECK(upgrade.extra == HF_STRATAGEM_EAGLE_500KG_BOMB);
    PressModalButton("Review purchase");
    CheckModalText("Extra stratagem: Eagle 500kg Bomb");
    PressModalButton("Destroyer");
    PressModalButton("##Defensive Upgrade", true);
    CheckModalText("Equipped defensive stratagems: -66% to cooldowns and unlimited uses.", "and Eagle rearm: +50%",
        "50 / 10");
    PressModalButton("##Support Upgrade", true);
    CheckModalText("Equipped support stratagems: -62.5% to cooldowns and unlimited uses.", "and Eagle rearm: -25%",
        "50 / 7");
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT && !upgrade.extra && !purchaseCalls);
    PressModalButton("##Support Upgrade", true);
    CHECK(!upgrade.selected);
    PressModalButton("##Support Upgrade", true);
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT);
    PressModalButton("Review purchase");
    CheckModalText("Total due:", "4750", "7");
    CheckModalText("40 / 2", "3 / 1");
    upgrade.balance[REQUISITION_SLIPS] = 4749;
    CheckModalText("Not enough requisition for this purchase.", "4749 / 2000");
    PressModalButton("Purchase");
    CHECK(!purchaseCalls);
    upgrade.balance[REQUISITION_SLIPS] = 5000;
    upgrade.balance[SUPER_SAMPLES] = 0;
    CheckModalText("0 / 1");
    PressModalButton("Purchase");
    CHECK(!purchaseCalls);
    upgrade.balance[SUPER_SAMPLES] = 3;
    PressModalButton("Destroyer");
    PressModalButton(" X ##Clear destroyer", true);
    CHECK(!upgrade.selected && !upgrade.extra && !editorapp::destroyerExtra);
    PressModalButton("Purchase##tab");
    CheckModalText("Total due:", "2750");
    CHECK(modalShown && modalChanges == opened && !purchaseCalls && !automaticReady);
    CHECK(CheckoutWindow() == checkoutWindow && checkoutWindow->Pos.x == checkoutPos.x && checkoutWindow->Pos.y == checkoutPos.y);
    PressModalButton("Destroyer");
    PressModalButton("##Support Upgrade", true);
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Draw();
    CHECK(!modalShown && !editorapp::shopOpen && !upgrade.selected && !upgrade.extra && !automaticReady);
    io.AddKeyEvent(ImGuiKey_Escape, false);

    due = 0;
    const unsigned supportReq = tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS];
    tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS] = 0;
    requested = true;
    editorapp::Tick();
    PressModalButton("##Support Upgrade", true);
    PressModalButton("Review purchase");
    CheckModalText("After purchase, you will ready automatically.", "Total due:", "7");
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    editorapp::Modal(nullptr, 1280, 720, 1.0f / 60);
    const std::string summary = GImGui->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::Render();
    const size_t total = summary.find("Total due:");
    CHECK(total != std::string::npos);
    const std::string totalLine = summary.substr(total, summary.find('\n', total) - total);
    CHECK(totalLine.find("7") != std::string::npos && totalLine.find("0") == std::string::npos);
    CHECK(upgrade.selected == SMR_UPGRADE_SUPPORT && !purchaseCalls && modalShown);
    editorapp::CloseCheckout();
    tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS] = supportReq;

    upgrade.available = 0;
    requested = true;
    editorapp::Tick();
    CheckModalText("Upgrade selection unavailable.");
    PressModalButton("##Support Upgrade", true);
    CHECK(!upgrade.selected);
    PressModalButton("Purchase##tab");
    CheckModalText("Nothing to purchase.");
    PressModalButton("Continue to Ready");
    CHECK(continueCalls == 1 && !purchaseCalls && modalShown && !automaticReady);
    editorapp::Tick();
    CHECK(!modalShown && automaticReady);
    automaticReady = false;

    upgrade.available = 1;
    requested = true;
    editorapp::Tick();
    CHECK(editorapp::destroyerSelection == 0 && !editorapp::destroyerExtra);
    Draw();
    PressModalButton("##Support Upgrade", true);
    PressModalButton("Review purchase");
    due = 0;
    CheckModalText("Total due:", "2000", "7");
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
    CheckExchangeControls(argc >= 2 ? argv[1] : nullptr);
    CHECK(ImGui::FindWindowByName("Spend My Resources - Currency Exchange")->Flags & ImGuiWindowFlags_NoMove);
    CHECK(!automaticReady && !editorapp::shopOpen);
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
    CheckKillCard();
    CheckKillSummary();
    CheckLayoutScreen();
    ImGui::DestroyContext();
    DeleteFileW(editorapp::panelPositionsPath.c_str());
    puts("PASS frontend: hover fades and sliding underline, live costs, F3 panel layout, checkout, and F2 exchange.");
}
