#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include "imgui_internal.h"
#include "../overlay/unbalance_frontend.cpp"

static int requested;
static unsigned logs;
static uint32_t gameState = HF_STATE_SHIP;

void UnbalanceRequestMitigation(int percent) { requested = percent; }
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
    const editorapp::Host host{&api, nullptr, {}, {}, nullptr, nullptr, nullptr};

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
