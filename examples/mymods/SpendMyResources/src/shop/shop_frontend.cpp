#include "frontend/frontend_internal.h"
#include "json.h"
#include "canvas.h"
#include "local_stats.h"
#include "sounds.h"

namespace editorapp
{
const HfApi* api;
HfMod* mod;
static void (*showModal)(bool);
bool shopOpen;
bool exchangeOpen;
static bool loadoutOpen;
void (*closeWindow)();

// The layout screen can sit over a checkout, so game input is held while either is open.
void SetScreens(bool shop, bool layout)
{
    const bool held = shopOpen || layoutOpen;
    shopOpen = shop;
    layoutOpen = layout;
    if ((shop || layout) != held) showModal(shop || layout);
}

void CloseCheckout(bool ready)
{
    SetScreens(false, layoutOpen);
    SmrFinishCheckout(ready);
}

void Init(const Host& host)
{
    api = host.api;
    mod = host.mod;
    showModal = host.modal;
    closeWindow = host.close;
    InitPanelPositions();
    std::wstring atlas;
    if (!host.modsFolder.empty())
        atlas = hfjson::Wide(host.modsFolder) + L"\\hellforge-editor\\game-icons.bin";
    if (!icons::Load(atlas) && api->log)
        api->log(mod, "No local game icon cache available; using fallback icons and named stratagem tiles.");
    const std::wstring images = hfjson::Wide(host.modFolder) + L"\\images\\";
    for (const char* file : kCurrencyIcons)
        if ((host.modFolder.empty() || !icons::AddImage(icons::MOD, file, images + hfjson::Wide(file))) && api->log)
            api->log(mod, "Currency icon %s is missing or unreadable; using its fallback.", file);
}

void Opened()
{
    if (shopOpen)
    {
        closeWindow();
        return;
    }
    if (layoutOpen) CloseLayout();
    exchangeOpen = true;
}
void Closed() { exchangeOpen = false; }

void Tick()
{
    FollowKills();
    FollowKillSummary();
    FollowLayoutKey();
    bool onShip = api->game_state(mod) == HF_STATE_SHIP;
    loadoutOpen = SmrLoadoutOpen(mod) != 0;
    if (exchangeOpen && !onShip)
    {
        closeWindow();
        exchangeOpen = false;
    }
    if (!loadoutOpen)
    {
        if (shopOpen) CloseCheckout();
        else SmrFinishCheckout(false);
        return;
    }
    if (SmrTakeCheckoutRequest())
    {
        if (exchangeOpen)
        {
            closeWindow();
            exchangeOpen = false;
        }
        checkoutPanel = CheckoutPanel::Destroyer;
        tabIndicator = 0.0f;
        destroyerSelection = 0;
        destroyerExtra = 0;
        helldiverSelection = SMR_PERK_PRIMARY_TRADEOFF;
        SetScreens(true, layoutOpen);
    }
    // Ready is pressed for the player only once the game has its input back.
    else if (shopOpen && !layoutOpen && SmrCheckoutComplete()) CloseCheckout(true);
}

float PixelScale(float, float) { return 1.0f; }
bool NoticeShown()
{
    return (loadoutOpen || KillCardShowing(killClock())) && !shopOpen && !exchangeOpen && !layoutOpen;
}
void AddModContent() {}
void ModsChanged() {}

void Overlay(ImDrawList*, float width, float height)
{
    if (!NoticeShown()) return;
    const ShopTheme theme;
    if (!loadoutOpen || KillSummaryShowing(killClock()))
    {
        KillRewardFrame(width, height);
        return;
    }
    SmrLoadoutQuote quote;
    bool valid = SmrQuoteLoadout(mod, &quote) != 0;
    PreparePanel(PositionPanel::LoadoutCost, width, height);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Loadout cost", nullptr, flags))
    {
        if (!valid)
            ImGui::TextUnformatted("Loadout cost unavailable");
        else
        {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "Loadout cost: %u Req", quote.total);
            if (quote.balance_ready) ImGui::TextDisabled("Available: %u Req", quote.balance);
            else ImGui::TextDisabled("Requisition balance unavailable");
            if (quote.count)
            {
                ImGui::Separator();
                for (size_t i = 0; i < quote.count; ++i)
                {
                    const SmrQuoteLine& item = quote.items[i];
                    if (item.due) ImGui::Text("%s - %u Req", item.name, item.due);
                    else ImGui::TextDisabled("%s - Access ready", item.name);
                }
            }
            if (quote.balance_ready && quote.total > quote.balance)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarning), "Not enough requisition");
        }
    }
    TrackPanel(PositionPanel::LoadoutCost);
    ImGui::End();
}

void Modal(ImDrawList*, float width, float height, float)
{
    // A checkout under the layout screen waits hidden; its purchases run in the shop, not in this frame.
    if (layoutOpen) LayoutFrame(width, height);
    else if (shopOpen) CheckoutFrame(width, height);
}
void SaveStatus(bool, const std::string&) {}
}

// The shop does not use the editor's fonts, sounds or stats saving.
namespace ui {
bool BuildFonts(float) { return false; }
const std::string& FontNote() { static const std::string none; return none; }
}
namespace localstats {
void SetSandboxSaveFolder(const std::wstring&) {}
bool Pending() { return false; }
bool Save(const std::string&, std::string*) { return false; }
}
namespace sounds {
bool Init(const HfApi*, HfMod*) { return false; }
bool Flush() { return false; }
bool Waiting() { return false; }
bool OnGameThread() { return false; }
}
