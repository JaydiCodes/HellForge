#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "app.h"
#include "json.h"
#include "unbalance_frontend.h"

namespace editorapp
{
static Host host;
static std::wstring settingsPath;
static int saved = UNBALANCE_DEFAULT_MITIGATION, draft = saved;
static bool saveFailed;
static constexpr const char* settingKey = "knockdown_mitigation_percent";

void Init(const Host& context)
{
    host = context;
    settingsPath.clear();
    saved = draft = UNBALANCE_DEFAULT_MITIGATION;
    saveFailed = false;
    UnbalanceRequestMitigation(saved);
    wchar_t local[MAX_PATH];
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!size || size >= MAX_PATH)
    {
        host.api->log(host.mod, "UnBalance Pass: could not locate LOCALAPPDATA for the mitigation setting.");
        return;
    }
    settingsPath = std::wstring(local, size) + L"\\hellforge-jedsdeads-unbalance-pass.json";
    std::string text, error;
    if (!hfjson::ReadFile(settingsPath, &text, &error))
    {
        if (error != "is missing") host.api->log(host.mod, "UnBalance Pass: cannot read mitigation setting: %s.", error.c_str());
        return;
    }

    hfjson::Value document;
    double value = hfjson::Parse(text, &document, nullptr) ? document.GetNumber(settingKey, -1) : -1;
    if (!(value >= 0 && value <= 100) || value != static_cast<int>(value))
    {
        host.api->log(host.mod, "UnBalance Pass: invalid mitigation setting; using the 70%% default.");
        return;
    }
    saved = static_cast<int>(value);
    UnbalanceRequestMitigation(saved);
}

static void Save()
{
    hfjson::Value document;
    document.At(settingKey) = hfjson::Value::MakeNumber(draft, true);
    saveFailed = settingsPath.empty() || !hfjson::WriteAtomically(settingsPath, hfjson::Write(document));
    if (saveFailed)
    {
        host.api->log(host.mod, "UnBalance Pass: could not save the mitigation setting. Previous setting kept.");
        draft = saved;
        return;
    }
    saved = draft;
    UnbalanceRequestMitigation(saved);
}

void Opened() { draft = saved; }
void Closed() {}

void Frame(ImDrawList*, float, float, float)
{
    if (!CanOpenOverlay(host.api, host.mod) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        host.close();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("JedsDead's UnBalance Pass", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        const auto state = UnbalanceMitigationStatus();
        const bool enabled = state == UNBALANCE_SOLO || state == UNBALANCE_REFUSED;
        if (!enabled) draft = saved;
        ImGui::TextUnformatted("Blunt-Force Mitigation");
        ImGui::BeginDisabled(!enabled);
        ImGui::SetNextItemWidth(350);
        ImGui::SliderInt("##knockdown", &draft, 0, 100, "%d%% knockdown mitigation", ImGuiSliderFlags_AlwaysClamp);
        if (enabled && open && ImGui::IsItemDeactivatedAfterEdit() && draft != saved) Save();
        ImGui::EndDisabled();
        if (state == UNBALANCE_COOP) ImGui::TextUnformatted("Solo only. Co-op uses the balance pass default.");
        if (state == UNBALANCE_WAITING) ImGui::TextUnformatted("Waiting for solo loadout and perk data.");
        if (state == UNBALANCE_REFUSED) ImGui::TextUnformatted("The game could not apply the saved setting.");
        if (state == UNBALANCE_RESTORE_REFUSED) ImGui::TextUnformatted("Could not restore the balance pass default.");
        if (saveFailed) ImGui::TextUnformatted("Could not save. Previous setting kept.");
    }
    ImGui::End();
    if (!open) host.close();
}

float PixelScale(float, float) { return 1.0f; }
bool NoticeShown() { return false; }
void AddModContent() {}
void ModsChanged() {}
void Tick() {}
void Overlay(ImDrawList*, float, float) {}
void Modal(ImDrawList*, float, float, float) {}
void SaveStatus(bool, const std::string&) {}
} // namespace editorapp
