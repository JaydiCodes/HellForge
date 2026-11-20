#include <windows.h>
#include "app.h"
#include "canvas.h"
#include "icons.h"
#include "local_stats.h"
#include "sounds.h"
#include "shop_frontend.h"
#include "profile_payment.cpp"
#include "prices.cpp"

namespace editorapp
{
static const HfApi* api;
static HfMod* mod;
static void (*showCheckout)(bool);
static bool shopOpen;
static bool purchasePending;
static bool exchangeOpen;
static void (*closeWindow)();

static void CloseCheckout(bool ready = false)
{
    showCheckout(false);
    SmrFinishCheckout(ready);
    shopOpen = false;
    purchasePending = false;
}

void Init(const Host& host)
{
    api = host.api;
    mod = host.mod;
    showCheckout = host.modal;
    closeWindow = host.close;
}

void Opened()
{
    if (shopOpen)
    {
        closeWindow();
        return;
    }
    exchangeOpen = true;
}
void Closed() { exchangeOpen = false; }
void Tick()
{
    uintptr_t menu = 0;
    uint32_t top = 0;
    uintptr_t game = api->module_base("game.dll", nullptr);
    bool loadout = game && !api->read(game + 0x347CE38, &menu, sizeof menu) && menu &&
        !api->read(menu, &top, sizeof top) && top == 11 && api->game_state(mod) == HF_STATE_SHIP;
    if (!loadout) SmrFinishCheckout(false);
    if (exchangeOpen && api->game_state(mod) != HF_STATE_SHIP)
    {
        closeWindow();
        exchangeOpen = false;
    }
    if (SmrTakeCheckoutRequest() && loadout)
    {
        if (exchangeOpen)
        {
            closeWindow();
            exchangeOpen = false;
        }
        shopOpen = true;
        purchasePending = false;
        showCheckout(true);
    }
    if (shopOpen)
    {
        unsigned total;
        int pending;
        SmrCheckout(&total, &pending);
        if (!loadout) CloseCheckout();
        else if (purchasePending && !pending && !total) CloseCheckout(true);
    }
}

static void CheckoutFrame(float width, float height)
{
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool visible = true;
    if (ImGui::Begin("Spend My Resources", &visible,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        unsigned balance = 0;
        bool ready = SmrBalance(&balance) != 0;
        if (ready) ImGui::Text("Requisition slips: %u", balance);
        else ImGui::TextDisabled("Requisition slips: unavailable");
        ImGui::TextDisabled("Purchases charge your saved offline profile.");
        ImGui::Separator();
        ImGui::TextUnformatted("Equipped restricted items");
        SmrItem item;
        for (size_t i = 0; SmrShopItem(i, &item); ++i)
        {
            if (!item.selected) continue;
            if (item.access) ImGui::Text("%s - Access ready (0 Req due)", item.name);
            else ImGui::Text("%s - %u Req", item.name, item.price);
        }
        unsigned total;
        int pending;
        bool canBuy = SmrCheckout(&total, &pending) != 0;
        ImGui::Separator();
        ImGui::Text("Total due: %u Req", total);
        ImGui::BeginDisabled(!canBuy);
        if (ImGui::Button(pending ? "Purchasing..." : "Purchase access"))
            purchasePending = SmrBuyCheckout(mod) != 0;
        ImGui::EndDisabled();
        ImGui::TextDisabled("After purchase, you will ready automatically.");
        char message[192];
        SmrMessage(message, sizeof message);
        ImGui::PushTextWrapPos(440);
        ImGui::TextWrapped("%s", message);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CloseCheckout();
}

void Frame(ImDrawList*, float width, float height, float)
{
    if (shopOpen) return;
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool visible = true;
    if (ImGui::Begin("Spend My Resources - Currency Exchange", &visible,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("Currency exchange (ship only)");
        ImGui::TextDisabled("Req 1 | Common 50 | Rare 100 | Super 200 | Forge Credits 500 points");
        SmrCurrency currency;
        int count = 0;
        bool ready = true, pending = false;
        while (SmrCurrencyItem(count, &currency))
        {
            if (!currency.ready) ready = false;
            pending |= currency.pending != 0;
            if (!currency.ready) ImGui::Text("%s: unavailable", currency.name);
            else if (currency.cap) ImGui::Text("%s: %u / %u", currency.name, currency.amount, currency.cap);
            else ImGui::Text("%s: %u", currency.name, currency.amount);
            ++count;
        }
        static int from = 0, to = 1, batches = 1;
        const auto choose = [](const char* label, int* selected)
        {
            SmrCurrency choice;
            if (!SmrCurrencyItem(*selected, &choice)) return;
            if (ImGui::BeginCombo(label, choice.name))
            {
                for (int i = 0; SmrCurrencyItem(i, &choice); ++i)
                    if (ImGui::Selectable(choice.name, i == *selected)) *selected = i;
                ImGui::EndCombo();
            }
        };
        ImGui::Separator();
        ImGui::BeginDisabled(pending);
        choose("Give", &from);
        choose("Receive", &to);
        ImGui::InputInt("Batches", &batches);
        ImGui::EndDisabled();
        unsigned debit = 0, credit = 0, unitDebit = 0, unitCredit = 0;
        SmrCurrency source = {}, target = {};
        bool quote = batches > 0 && SmrExchangeQuote(from, to, static_cast<unsigned>(batches), &debit, &credit);
        SmrCurrencyItem(from, &source);
        SmrCurrencyItem(to, &target);
        if (SmrExchangeQuote(from, to, 1, &unitDebit, &unitCredit))
            ImGui::Text("One batch: %u %s = %u %s", unitDebit, source.name, unitCredit, target.name);
        if (quote) ImGui::Text("Give %u %s; receive %u %s", debit, source.name, credit, target.name);
        bool funds = quote && source.amount >= debit;
        bool room = quote && static_cast<uint64_t>(target.amount) + credit <=
            (target.cap ? target.cap : UINT32_MAX);
        if (!quote) ImGui::TextDisabled("Choose different currencies and a positive, valid batch count.");
        else if (!funds) ImGui::TextDisabled("Insufficient source currency.");
        else if (!room) ImGui::TextDisabled("This exchange would exceed the destination cap.");
        ImGui::BeginDisabled(!ready || pending || !funds || !room);
        if (ImGui::Button(pending ? "Processing..." : "Exchange"))
            SmrExchange(mod, from, to, static_cast<unsigned>(batches));
        ImGui::EndDisabled();
        ImGui::TextDisabled("Exact whole-unit batches only. Unspent currency stays in your balance.");
        char message[192];
        SmrMessage(message, sizeof message);
        ImGui::PushTextWrapPos(520);
        ImGui::TextWrapped("%s", message);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        closeWindow();
        exchangeOpen = false;
    }
}

float PixelScale(float, float) { return 1.0f; }
bool NoticeShown() { return false; }
void AddModContent() {}
void ModsChanged() {}
void Overlay(ImDrawList*, float, float) {}
void Modal(ImDrawList*, float width, float height, float) { CheckoutFrame(width, height); }
void SaveStatus(bool, const std::string&) {}
}

// The shop uses plain text/buttons, with no editor assets, sounds or stats saving.
namespace ui { bool BuildFonts(float) { return false; } }
namespace icons {
bool Upload11(ID3D11Device*) { return true; }
bool Upload(ID3D12Device*, ID3D12CommandQueue*, ExecuteFn, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) { return true; }
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
