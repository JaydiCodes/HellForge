#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include "imgui_internal.h"
#include "../overlay/unbalance_frontend.cpp"

static int requested;
static int swapsRequested = -1;
static unsigned logs;
static uint32_t gameState = HF_STATE_SHIP;

void UnbalanceRequestMitigation(int percent) { requested = percent; }
void UnbalanceSetProjectileSwaps(int enabled) { swapsRequested = enabled; }
UnbalanceMitigationState UnbalanceMitigationStatus() { return UNBALANCE_SOLO; }
static void Log(HfMod*, const char*, ...) { ++logs; }
static uint32_t State(HfMod*) { return gameState; }

int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto directory = hfjson::Wide(argv[1]);
    assert(SetEnvironmentVariableW(L"LOCALAPPDATA", directory.c_str()));
    HfApi api{};
    api.log = Log;
    api.game_state = State;
    const editorapp::Host host{&api, nullptr, argv[1], {}, nullptr, nullptr, nullptr};

    assert(CanOpenOverlay(&api, nullptr) && hf_overlay_hotkey == VK_F7);
    gameState = HF_STATE_PREPARE_MISSION;
    assert(!CanOpenOverlay(&api, nullptr));
    gameState = HF_STATE_MISSION;
    assert(CanOpenOverlay(&api, nullptr));

    editorapp::Init(host);
    assert(requested == 60 && editorapp::draft == 60 && logs == 0);
    editorapp::draft = 85;
    editorapp::Save();
    assert(requested == 85 && !editorapp::saveFailed);
    const auto path = editorapp::settingsPath;
    editorapp::Init(host);
    assert(editorapp::draft == 85);
    editorapp::Opened();
    assert(requested == 85 && editorapp::draft == 85);

    editorapp::settingsPath = directory + L"\\missing\\settings.json";
    editorapp::draft = 42;
    editorapp::Save();
    assert(editorapp::saveFailed && editorapp::draft == 85 && requested == 85 && logs == 1);

    assert(hfjson::WriteAtomically(path, R"({"knockdown_mitigation_percent":100.5})"));
    editorapp::Init(host);
    assert(requested == 60 && logs == 2);
    for (int percent : {0, 60, 70, 100})
    {
        editorapp::draft = percent;
        editorapp::Save();
        editorapp::Init(host);
        assert(requested == percent && editorapp::draft == percent && !editorapp::saveFailed);
    }
    editorapp::draft = 60;
    editorapp::Save();

    /* The stats switches: hellforge-stats.json is regenerated from the built-in pass, and left alone on defaults. */
    const std::wstring statsFile = editorapp::statsPath;
    assert(editorapp::masterParsed && !editorapp::sections.empty());
    assert(statsFile == directory + L"\\hellforge-stats.json");
    assert(editorapp::statsEnabled && editorapp::swapsEnabled && swapsRequested == 1);
    assert(GetFileAttributesW(statsFile.c_str()) == INVALID_FILE_ATTRIBUTES);
    auto targetNamed = [](const char* section, const char* name) -> editorapp::StatsTarget* {
        editorapp::StatsSection* s = editorapp::SectionNamed(section);
        if (s == nullptr) return nullptr;
        for (editorapp::StatsTarget& t : s->targets)
            if (t.name == name) return &t;
        return nullptr;
    };
    editorapp::StatsTarget* eagle = targetNamed("enemies", "eagle_bomb");
    assert(eagle != nullptr && editorapp::SectionNamed("weapons") != nullptr);

    /* One entry off: the file appears without it, everything else kept. */
    eagle->enabled = false;
    editorapp::SaveToggles();
    assert(!editorapp::saveFailed && !editorapp::statsWriteFailed);
    std::string text, again;
    hfjson::Value stats;
    assert(hfjson::ReadFile(statsFile, &text) && hfjson::Parse(text, &stats, nullptr));
    assert(stats.GetNumber("format", 0) == 1);
    assert(stats.Find("enemies") != nullptr && stats.Find("enemies")->Find("eagle_bomb") == nullptr);
    assert(stats.Find("enemies")->Find("Charger") != nullptr && stats.Find("weapons") != nullptr);

    /* The switch comes back after a restart, and the regenerated file is not written again. */
    editorapp::Init(host);
    eagle = targetNamed("enemies", "eagle_bomb");
    assert(eagle != nullptr && !eagle->enabled && editorapp::statsEnabled);
    assert(hfjson::ReadFile(statsFile, &again) && again == text);

    /* A reinstall ships the full file again: Init filters it back to the saved switches. */
    assert(hfjson::WriteAtomically(statsFile, kUnbalanceMasterStats));
    editorapp::Init(host);
    assert(hfjson::ReadFile(statsFile, &again) && again == text);

    /* A whole section off. */
    editorapp::SectionNamed("enemies")->enabled = false;
    editorapp::SaveToggles();
    assert(hfjson::ReadFile(statsFile, &text) && hfjson::Parse(text, &stats, nullptr));
    assert(stats.Find("enemies") == nullptr && stats.Find("weapons") != nullptr);

    /* The weapons section and the master switch gate the code's projectile swaps too. */
    editorapp::SectionNamed("weapons")->enabled = false;
    editorapp::SaveToggles();
    assert(swapsRequested == 0);
    editorapp::SectionNamed("weapons")->enabled = true;
    editorapp::SaveToggles();
    assert(swapsRequested == 1);
    editorapp::swapsEnabled = false;
    editorapp::SaveToggles();
    assert(swapsRequested == 0);
    editorapp::swapsEnabled = true;

    /* Everything off: the stats file goes; back on, the whole pass is written again. */
    editorapp::statsEnabled = false;
    editorapp::SaveToggles();
    assert(swapsRequested == 0 && GetFileAttributesW(statsFile.c_str()) == INVALID_FILE_ATTRIBUTES);
    editorapp::statsEnabled = true;
    editorapp::SectionNamed("enemies")->enabled = true;
    targetNamed("enemies", "eagle_bomb")->enabled = true;
    editorapp::SaveToggles();
    assert(swapsRequested == 1 && editorapp::TogglesDefault());
    assert(hfjson::ReadFile(statsFile, &text) && hfjson::Parse(text, &stats, nullptr));
    assert(stats.Find("enemies") != nullptr && stats.Find("enemies")->Find("eagle_bomb") != nullptr);
    editorapp::Init(host);
    assert(hfjson::ReadFile(statsFile, &again) && again == text); /* defaults: the shipped file is left alone */
    assert(DeleteFileW(statsFile.c_str()));

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    editorapp::Frame(nullptr, 800, 600, 1);
    ImGui::Render();
    auto* window = ImGui::FindWindowByName("JedsDead's UnBalance Pass");
    const auto& style = ImGui::GetStyle();
    io.AddMousePosEvent(window->Pos.x + 200, window->Pos.y + window->TitleBarHeight +
        style.WindowPadding.y + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeight() / 2);
    ImGui::NewFrame();
    editorapp::Frame(nullptr, 800, 600, 1);
    ImGui::Render();
    io.AddMouseButtonEvent(0, true);
    ImGui::NewFrame();
    editorapp::Frame(nullptr, 800, 600, 1);
    ImGui::Render();
    assert(io.MouseDown[0] && editorapp::saved != 60 && requested == editorapp::draft);
    const int selected = requested;
    ImGui::DestroyContext();
    editorapp::Init(host);
    assert(requested == selected && editorapp::draft == selected);
    assert(DeleteFileW(path.c_str()));
    puts("UnBalance Pass settings tests passed");
}
