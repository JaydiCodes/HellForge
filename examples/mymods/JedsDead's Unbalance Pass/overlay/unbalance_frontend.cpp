#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include "app.h"
#include "json.h"
#include "unbalance_frontend.h"
#include "unbalance_stats.inc"

namespace editorapp
{
static Host host;
static std::wstring settingsPath;
static int saved = UNBALANCE_DEFAULT_MITIGATION, draft = saved;
static bool saveFailed;
static constexpr const char* settingKey = "knockdown_mitigation_percent";

/* The F7 menu's switches over the balance pass: every section and entry of the built-in stats, each on by default. */
struct StatsTarget { std::string name; bool enabled; };
struct StatsSection { std::string name; bool enabled; std::vector<StatsTarget> targets; };
static hfjson::Value settings;   /* the settings file's document, written back with every change */
static hfjson::Value master;     /* the full balance pass, as built into the DLL */
static bool masterParsed;
static std::vector<StatsSection> sections;
static bool statsEnabled = true; /* the "all together" switch */
static bool swapsEnabled = true; /* the gameplay code's projectile swaps, shown under weapons */
static std::wstring statsPath;
static bool statsWriteFailed;

static StatsSection* SectionNamed(const std::string& name)
{
    for (StatsSection& section : sections)
        if (section.name == name) return &section;
    return nullptr;
}

static bool SectionOn(const std::string& name)
{
    const StatsSection* section = SectionNamed(name);
    return section == nullptr || section->enabled;
}

static int EffectiveSwaps() { return statsEnabled && swapsEnabled && SectionOn("weapons"); }

static void LoadMaster()
{
    sections.clear();
    std::string error;
    masterParsed = hfjson::Parse(kUnbalanceMasterStats, &master, &error);
    if (!masterParsed)
    {
        host.api->log(host.mod, "UnBalance Pass: the built-in stats did not parse: %s.", error.c_str());
        return;
    }
    for (const auto& member : master.members)
    {
        if (member.first == "format" || member.second.kind != hfjson::Value::Object) continue;
        StatsSection section{member.first, true, {}};
        for (const auto& target : member.second.members) section.targets.push_back({target.first, true});
        sections.push_back(std::move(section));
    }
}

/* The saved switches onto the sections; a name the built-in stats no longer have is dropped. */
static void LoadToggles()
{
    statsEnabled = settings.GetBool("stats_enabled", true);
    swapsEnabled = settings.GetBool("projectile_swaps_enabled", true);
    const hfjson::Value* off = settings.Find("disabled_sections", hfjson::Value::Array);
    if (off != nullptr)
        for (const hfjson::Value& item : off->items)
            if (StatsSection* section = item.kind == hfjson::Value::String ? SectionNamed(item.text) : nullptr)
                section->enabled = false;
    const hfjson::Value* changes = settings.Find("disabled_changes", hfjson::Value::Object);
    if (changes == nullptr) return;
    for (const auto& member : changes->members)
    {
        StatsSection* section = member.second.kind == hfjson::Value::Array ? SectionNamed(member.first) : nullptr;
        if (section == nullptr) continue;
        for (const hfjson::Value& item : member.second.items)
            if (item.kind == hfjson::Value::String)
                for (StatsTarget& target : section->targets)
                    if (target.name == item.text) target.enabled = false;
    }
}

/* Whether hellforge-stats.json would hold the whole pass: then the shipped file is left alone. */
static bool TogglesDefault()
{
    if (!statsEnabled) return false;
    for (const StatsSection& section : sections)
    {
        if (!section.enabled) return false;
        for (const StatsTarget& target : section.targets)
            if (!target.enabled) return false;
    }
    return true;
}

/* The stats file with the disabled parts left out; false when nothing of it is on (the file should be gone). */
static bool DesiredStats(std::string* text)
{
    if (!statsEnabled) return false;
    hfjson::Value document = master;
    bool any = false;
    for (const StatsSection& s : sections)
    {
        if (!s.enabled) { document.Erase(s.name); continue; }
        hfjson::Value* section = document.Find(s.name);
        if (section == nullptr) continue;
        for (const StatsTarget& target : s.targets)
            if (!target.enabled) section->Erase(target.name);
        if (section->members.empty()) document.Erase(s.name);
        else any = true;
    }
    if (!any) return false;
    *text = hfjson::Write(document);
    return true;
}

/* Rewrites the mod folder's hellforge-stats.json to the enabled changes (or removes it, everything off). The launcher
 * follows the mods folder: with live mod changes on the new set applies at once, else to the next session. */
static void SyncStats()
{
    statsWriteFailed = false;
    if (statsPath.empty() || !masterParsed) return;
    std::string desired, current;
    if (!DesiredStats(&desired))
        statsWriteFailed = !DeleteFileW(statsPath.c_str()) &&
            GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND;
    else if (!hfjson::ReadFile(statsPath, &current) || current != desired)
        statsWriteFailed = !hfjson::WriteAtomically(statsPath, desired);
    if (statsWriteFailed)
        host.api->log(host.mod, "UnBalance Pass: could not update hellforge-stats.json; the game keeps the previous changes.");
}

void Init(const Host& context)
{
    host = context;
    settingsPath.clear();
    statsPath.clear();
    saved = draft = UNBALANCE_DEFAULT_MITIGATION;
    saveFailed = statsWriteFailed = false;
    statsEnabled = swapsEnabled = true;
    settings = hfjson::Value();
    settings.kind = hfjson::Value::Object;
    LoadMaster();
    if (!host.modFolder.empty()) statsPath = hfjson::Wide(host.modFolder) + L"\\hellforge-stats.json";
    UnbalanceRequestMitigation(saved);
    UnbalanceSetProjectileSwaps(1);
    wchar_t local[MAX_PATH];
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!size || size >= MAX_PATH)
    {
        host.api->log(host.mod, "UnBalance Pass: could not locate LOCALAPPDATA for the mod's settings.");
        return;
    }
    settingsPath = std::wstring(local, size) + L"\\hellforge-jedsdeads-unbalance-pass.json";
    std::string text, error;
    if (!hfjson::ReadFile(settingsPath, &text, &error))
    {
        if (error != "is missing") host.api->log(host.mod, "UnBalance Pass: cannot read the mod's settings: %s.", error.c_str());
        return;
    }
    if (!hfjson::Parse(text, &settings, nullptr) || settings.kind != hfjson::Value::Object)
    {
        settings = hfjson::Value();
        settings.kind = hfjson::Value::Object;
        host.api->log(host.mod, "UnBalance Pass: invalid mitigation setting; using the %d%% default.", UNBALANCE_DEFAULT_MITIGATION);
        return;
    }
    double value = settings.GetNumber(settingKey, -1);
    if (value >= 0 && value <= 100 && value == static_cast<int>(value))
    {
        saved = draft = static_cast<int>(value);
        UnbalanceRequestMitigation(saved);
    }
    else host.api->log(host.mod, "UnBalance Pass: invalid mitigation setting; using the %d%% default.", UNBALANCE_DEFAULT_MITIGATION);
    LoadToggles();
    UnbalanceSetProjectileSwaps(EffectiveSwaps());
    if (!TogglesDefault()) SyncStats(); /* the defaults leave the shipped file alone (manual tweaks stay) */
}

static bool WriteSettings()
{
    settings.kind = hfjson::Value::Object;
    settings.At(settingKey) = hfjson::Value::MakeNumber(draft, true);
    settings.At("stats_enabled") = hfjson::Value::MakeBool(statsEnabled);
    settings.At("projectile_swaps_enabled") = hfjson::Value::MakeBool(swapsEnabled);
    hfjson::Value offSections, offChanges;
    offSections.kind = hfjson::Value::Array;
    offChanges.kind = hfjson::Value::Object;
    for (const StatsSection& section : sections)
    {
        if (!section.enabled) offSections.items.push_back(hfjson::Value::MakeString(section.name));
        hfjson::Value targets;
        targets.kind = hfjson::Value::Array;
        for (const StatsTarget& target : section.targets)
            if (!target.enabled) targets.items.push_back(hfjson::Value::MakeString(target.name));
        if (!targets.items.empty()) offChanges.At(section.name) = targets;
    }
    if (offSections.items.empty()) settings.Erase("disabled_sections");
    else settings.At("disabled_sections") = offSections;
    if (offChanges.members.empty()) settings.Erase("disabled_changes");
    else settings.At("disabled_changes") = offChanges;
    return !settingsPath.empty() && hfjson::WriteAtomically(settingsPath, hfjson::Write(settings));
}

/* The slider's save: a failure puts the shown value back. */
static void Save()
{
    saveFailed = !WriteSettings();
    if (saveFailed)
    {
        host.api->log(host.mod, "UnBalance Pass: could not save the mitigation setting. Previous setting kept.");
        draft = saved;
        return;
    }
    saved = draft;
    UnbalanceRequestMitigation(saved);
}

/* A switch flipped: saved, the stats file follows, the gameplay side told. */
static void SaveToggles()
{
    saveFailed = !WriteSettings();
    if (saveFailed) host.api->log(host.mod, "UnBalance Pass: could not save the mod's settings. Previous settings kept on disk.");
    SyncStats();
    UnbalanceSetProjectileSwaps(EffectiveSwaps());
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
        if (ImGui::SliderInt("##knockdown", &draft, 0, 100, "%d%% knockdown mitigation", ImGuiSliderFlags_AlwaysClamp) &&
            enabled && draft != saved) Save();
        ImGui::EndDisabled();
        if (state == UNBALANCE_COOP) ImGui::TextUnformatted("Solo only. Co-op uses the balance pass default.");
        if (state == UNBALANCE_WAITING) ImGui::TextUnformatted("Waiting for solo loadout and perk data.");
        if (state == UNBALANCE_REFUSED) ImGui::TextUnformatted("The game could not apply the saved setting.");
        if (state == UNBALANCE_RESTORE_REFUSED) ImGui::TextUnformatted("Could not restore the balance pass default.");

        ImGui::Separator();
        ImGui::TextUnformatted("Balance pass changes");
        bool changed = ImGui::Checkbox("All changes", &statsEnabled);
        ImGui::BeginDisabled(!statsEnabled);
        if (ImGui::BeginChild("sections", ImVec2(350, 240), ImGuiChildFlags_Borders))
            for (StatsSection& section : sections)
            {
                ImGui::PushID(section.name.c_str());
                changed |= ImGui::Checkbox("##section", &section.enabled);
                ImGui::SameLine();
                if (ImGui::TreeNode(section.name.c_str()))
                {
                    ImGui::BeginDisabled(!section.enabled);
                    for (StatsTarget& target : section.targets) changed |= ImGui::Checkbox(target.name.c_str(), &target.enabled);
                    if (section.name == "weapons") changed |= ImGui::Checkbox("projectile swaps (code)", &swapsEnabled);
                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::EndDisabled();
        if (changed) SaveToggles();
        ImGui::TextWrapped("Switches save at once. They reach the running game only with the launcher's live mod changes on; otherwise they apply to the next session.");
        if (statsWriteFailed) ImGui::TextUnformatted("Could not update hellforge-stats.json.");
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
