#include <windows.h>
#include "app.h"
#include "canvas.h"
#include "icons.h"
#include "local_stats.h"
#include "sounds.h"
#include "shop_frontend.h"

namespace editorapp
{
static const HfApi* api;
static HfMod* mod;
static void (*closeShop)();
static bool shopOpen;

void Init(const Host& host)
{
    api = host.api;
    mod = host.mod;
    closeShop = host.close;
}

void Opened() { shopOpen = true; }
void Closed() { shopOpen = false; }
void Tick()
{
    if (shopOpen && !CanOpenOverlay(api, mod)) closeShop();
}

void Frame(ImDrawList*, float width, float height, float)
{
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool visible = true;
    if (ImGui::Begin("Spend My Resources - Test Shop", &visible,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        unsigned balance = SmrBalance();
        ImGui::Text("Test balance: %u Requisition", balance);
        ImGui::TextDisabled("Simulation only. Real Requisition is not charged.");
        ImGui::Separator();
        SmrItem item;
        for (size_t i = 0; SmrShopItem(i, &item); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s - %u Req", item.name, item.price);
            ImGui::SameLine(290);
            ImGui::BeginDisabled(item.access || balance < item.price);
            const char* label = item.access ? "Access ready" : balance < item.price ? "Not enough" : "Buy test access";
            if (ImGui::Button(label, ImVec2(125, 0)) && SmrBuyAccess(mod, i)) balance = SmrBalance();
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
    ImGui::End();
    if (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) closeShop();
}

float PixelScale(float, float) { return 1.0f; }
bool NoticeShown() { return false; }
void AddModContent() {}
void ModsChanged() {}
void Overlay(ImDrawList*, float, float) {}
void Modal(ImDrawList*, float, float, float) {}
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
