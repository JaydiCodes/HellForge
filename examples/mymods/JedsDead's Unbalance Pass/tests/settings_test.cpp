#include <cassert>
#include <cstdio>
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
    assert(requested == 70 && logs == 0);
    editorapp::draft = 85;
    editorapp::Save();
    assert(requested == 85 && !editorapp::saveFailed);
    const auto path = editorapp::settingsPath;
    editorapp::Init(host);
    editorapp::Opened();
    assert(requested == 85 && editorapp::draft == 85);

    editorapp::settingsPath = directory + L"\\missing\\settings.json";
    editorapp::draft = 42;
    editorapp::Save();
    assert(editorapp::saveFailed && editorapp::draft == 85 && requested == 85 && logs == 1);

    assert(hfjson::WriteAtomically(path, R"({"knockdown_mitigation_percent":100.5})"));
    editorapp::Init(host);
    assert(requested == 70 && logs == 2);
    assert(DeleteFileW(path.c_str()));
    puts("UnBalance Pass settings tests passed");
}
