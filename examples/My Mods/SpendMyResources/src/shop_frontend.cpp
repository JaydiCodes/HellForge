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
}

void Opened() { shopOpen = true; }
void Closed() { shopOpen = false; }
void Tick()
{
    uintptr_t menu = 0;
    uint32_t top = 0;
    uintptr_t game = api->module_base("game.dll", nullptr);
    bool loadout = game && !api->read(game + 0x347CE38, &menu, sizeof menu) && menu &&
        !api->read(menu, &top, sizeof top) && top == 11 && api->game_state(mod) == HF_STATE_SHIP;
    if (!loadout) SmrFinishCheckout(false);
    if (SmrTakeCheckoutRequest() && loadout)
    {
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

void Frame(ImDrawList*, float width, float height, float)
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

float PixelScale(float, float) { return 1.0f; }
bool NoticeShown() { return false; }
void AddModContent() {}
void ModsChanged() {}
void Overlay(ImDrawList*, float, float) {}
void Modal(ImDrawList* list, float width, float height, float dt) { Frame(list, width, height, dt); }
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
