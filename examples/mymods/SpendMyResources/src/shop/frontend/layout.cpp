#include "frontend_internal.h"
#include "json.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <limits>

namespace editorapp
{
PanelPosition panelPositions[kPanelCount];
std::wstring panelPositionsPath;
bool panelPositionsLoaded;

/* F3's layout screen: a preview of each panel, dragged into place and saved on leaving. */
struct LayoutPreview
{
    ImVec2 position;
    bool grabbed, moved;
};
static LayoutPreview layoutPreviews[IM_ARRAYSIZE(panelPositions)];
bool layoutOpen;
static bool layoutKeyWasDown;
static bool F3Down()
{
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId() && (GetAsyncKeyState(VK_F3) & 0x8000);
}
// The host passes keys to the frontend only while it holds input, so F3 is polled to open the screen.
bool (*layoutKeyDown)() = F3Down;

static const char* PositionName(PositionPanel panel)
{
    static constexpr const char* names[] = {"checkout", "exchange", "loadout_cost", "kill_reward"};
    static_assert(static_cast<size_t>(IM_ARRAYSIZE(names)) == kPanelCount, "Each panel needs a position key.");
    return names[PositionIndex(panel)];
}

static bool ReadPosition(const hfjson::Value* value, ImVec2* position)
{
    if (!value || value->kind != hfjson::Value::Object) return false;
    const hfjson::Value* x = value->Find("x");
    const hfjson::Value* y = value->Find("y");
    if (!x || x->kind != hfjson::Value::Number || !y || y->kind != hfjson::Value::Number) return false;
    const double px = x->AsNumber(std::numeric_limits<double>::quiet_NaN());
    const double py = y->AsNumber(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(px) || !std::isfinite(py) ||
        std::abs(px) > std::numeric_limits<float>::max() || std::abs(py) > std::numeric_limits<float>::max())
        return false;
    *position = ImVec2(static_cast<float>(px), static_cast<float>(py));
    return true;
}

void InitPanelPositions()
{
    DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed)
    {
        std::wstring folder(needed, L'\0');
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", &folder[0], needed);
        if (length && length < needed)
        {
            folder.resize(length);
            panelPositionsPath = folder + L"\\SpendMyResources\\ui-layout.json";
        }
    }
}

void LoadPanelPositions()
{
    if (panelPositionsLoaded) return;
    panelPositionsLoaded = true;
    if (panelPositionsPath.empty()) return;

    std::string text, why;
    if (!hfjson::ReadFile(panelPositionsPath, &text, &why))
    {
        if (why != "is missing" && api->log)
            api->log(mod, "Cannot read Spend My Resources UI positions: %s.", why.c_str());
        return;
    }

    hfjson::Value document;
    if (!hfjson::Parse(text, &document, &why) || document.kind != hfjson::Value::Object ||
        document.GetNumber("version", -1) != 1)
    {
        if (api->log) api->log(mod, "Spend My Resources UI positions are invalid; using default positions.");
        return;
    }
    for (size_t i = 0; i < IM_ARRAYSIZE(panelPositions); ++i)
    {
        const hfjson::Value* value = document.Find(PositionName(static_cast<PositionPanel>(i)));
        if (!value) continue;
        if (ReadPosition(value, &panelPositions[i].position))
            panelPositions[i].hasPosition = true;
        else if (api->log)
            api->log(mod, "Spend My Resources UI position '%s' is invalid; using its default.", PositionName(static_cast<PositionPanel>(i)));
    }
}

void SavePanelPositions()
{
    if (panelPositionsPath.empty())
    {
        if (api->log) api->log(mod, "Cannot save Spend My Resources UI positions: LOCALAPPDATA is unavailable.");
        return;
    }
    const std::wstring folder = panelPositionsPath.substr(0, panelPositionsPath.find_last_of(L'\\'));
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        if (api->log) api->log(mod, "Cannot create the Spend My Resources UI settings folder.");
        return;
    }
    hfjson::Value document;
    document.kind = hfjson::Value::Object;
    document.At("version") = hfjson::Value::MakeNumber(1, true);
    for (size_t i = 0; i < IM_ARRAYSIZE(panelPositions); ++i)
    {
        const PanelPosition& panel = panelPositions[i];
        if (!panel.hasPosition) continue;
        hfjson::Value& value = document.At(PositionName(static_cast<PositionPanel>(i)));
        value.kind = hfjson::Value::Object;
        value.At("x") = hfjson::Value::MakeNumber(panel.position.x, false);
        value.At("y") = hfjson::Value::MakeNumber(panel.position.y, false);
    }
    if (!hfjson::WriteAtomically(panelPositionsPath, hfjson::Write(document)) && api->log)
        api->log(mod, "Cannot save Spend My Resources UI positions.");
}

static bool SavedPosition(PositionPanel panel, float width, float height, ImVec2* out)
{
    const PanelPosition& state = panelPositions[PositionIndex(panel)];
    if (!state.hasPosition) return false;
    *out = ImVec2(std::clamp(state.position.x, 0.0f, std::max(0.0f, width - 80.0f)),
        std::clamp(state.position.y, 0.0f, std::max(0.0f, height - 40.0f)));
    return true;
}

// The kill reward card sits where the loadout cost card does until it is given a place of its own.
void PanelPlace(PositionPanel panel, float width, float height, ImVec2* position, ImVec2* pivot)
{
    LoadPanelPositions();
    *pivot = ImVec2(0, 0);
    if (SavedPosition(panel, width, height, position) ||
        (panel == PositionPanel::KillReward && SavedPosition(PositionPanel::LoadoutCost, width, height, position)))
        return;
    const bool card = panel == PositionPanel::LoadoutCost || panel == PositionPanel::KillReward;
    *position = card ? ImVec2(width / 2, 16) : ImVec2(width / 2, height / 2);
    *pivot = card ? ImVec2(0.5f, 0) : ImVec2(0.5f, 0.5f);
}

void PreparePanel(PositionPanel panel, float width, float height)
{
    PanelPosition& state = panelPositions[PositionIndex(panel)];
    if (state.initialized) return;
    state.initialized = true;
    ImVec2 position, pivot;
    PanelPlace(panel, width, height, &position, &pivot);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
}

void TrackPanel(PositionPanel panel)
{
    panelPositions[PositionIndex(panel)].size = ImGui::GetWindowSize();
}

void OpenLayout()
{
    if (exchangeOpen)
    {
        closeWindow();
        exchangeOpen = false;
    }
    for (LayoutPreview& preview : layoutPreviews) preview = {};
    SetScreens(shopOpen, true);
}

// A panel that was not dragged keeps what it had: its saved position, or its default anchor.
void CloseLayout()
{
    SetScreens(shopOpen, false);
    for (size_t i = 0; i < IM_ARRAYSIZE(panelPositions); ++i)
    {
        panelPositions[i].initialized = false;
        if (!layoutPreviews[i].moved) continue;
        panelPositions[i].position = layoutPreviews[i].position;
        panelPositions[i].hasPosition = true;
    }
    SavePanelPositions();
}

void FollowLayoutKey()
{
    const bool down = layoutKeyDown();
    const bool pressed = down && !layoutKeyWasDown;
    layoutKeyWasDown = down;
    if (!pressed) return;
    if (layoutOpen) CloseLayout();
    else OpenLayout();
}

static void LayoutPanel(PositionPanel panel, float width, float height)
{
    static constexpr struct { const char* window; const char* title; const char* about; } kPreviews[] = {
        {"Spend My Resources###layout-checkout", "Checkout", "Opens when you press Ready with gear or upgrades to buy."},
        {"Spend My Resources - Currency Exchange###layout-exchange", "Currency exchange", "Opens with F2 on the ship."},
        {"###layout-loadout-cost", "Loadout cost", "Shows while the loadout picker is open."},
        {"###layout-kill-reward", "Kill reward", "Shows for 5 seconds after each counted kill."},
    };
    static_assert(static_cast<size_t>(IM_ARRAYSIZE(kPreviews)) == kPanelCount, "Each panel needs a layout preview.");
    const size_t i = PositionIndex(panel);
    LayoutPreview& preview = layoutPreviews[i];
    // Until dragged, the preview stays on the panel's anchor while its auto-fitted size settles. The drag is seen
    // before Begin, so the move ImGui made this frame is not placed back.
    if (preview.grabbed && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) preview.moved = true;
    if (!preview.moved)
    {
        ImVec2 place, pivot;
        PanelPlace(panel, width, height, &place, &pivot);
        ImGui::SetNextWindowPos(place, ImGuiCond_Always, pivot);
    }
    const bool checkout = panel == PositionPanel::Checkout;
    const bool titled = checkout || panel == PositionPanel::Exchange;
    if (checkout) ImGui::SetNextWindowSize(CheckoutSize(width, height), ImGuiCond_Always);
    else
    {
        // The preview is as large as the panel was when last shown, so it is placed by its real outline.
        const ImVec2 seen = panelPositions[i].size;
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(seen.x, 220.0f), seen.y), ImVec2(FLT_MAX, FLT_MAX));
    }
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
        (checkout ? 0 : ImGuiWindowFlags_AlwaysAutoResize) | (titled ? 0 : ImGuiWindowFlags_NoTitleBar);
    if (ImGui::Begin(kPreviews[i].window, nullptr, flags))
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", kPreviews[i].title);
        ImGui::TextDisabled("%s", kPreviews[i].about);
    }
    // Only text is inside, so a press on the preview grabs the window.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) preview.grabbed = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) preview.grabbed = false;
    preview.position = ImGui::GetWindowPos();
    ImGui::End();
}

static void LayoutHelp(float width, float height)
{
    static constexpr const char* kLines[] = {
        "UI panel layout",
        "Drag the Spend My Resources panels to move them.",
        "Press F3 or Esc to save their positions and leave.",
    };
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 text(0, 0);
    for (const char* line : kLines)
    {
        const ImVec2 size = ImGui::CalcTextSize(line);
        text = ImVec2(std::max(text.x, size.x), text.y + size.y + style.ItemSpacing.y);
    }
    const ImVec2 pad(18, 14);
    const ImVec2 min(width / 2 - text.x / 2 - pad.x, height / 2 - text.y / 2 - pad.y);
    const ImVec2 max(width / 2 + text.x / 2 + pad.x, height / 2 + text.y / 2 + pad.y);
    // The foreground list keeps the box on top without taking clicks from panels under it.
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(min, max, IM_COL32(15, 19, 25, 235));
    draw->AddRect(min, max, kAccent);
    float y = min.y + pad.y;
    for (size_t i = 0; i < IM_ARRAYSIZE(kLines); ++i)
    {
        const ImVec2 size = ImGui::CalcTextSize(kLines[i]);
        draw->AddText(ImVec2(width / 2 - size.x / 2, y), i ? kText : kAccentText, kLines[i]);
        y += size.y + style.ItemSpacing.y;
    }
}

void LayoutFrame(float width, float height)
{
    const ShopTheme theme;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(0, 0, 0, 110));
    for (size_t i = 0; i < IM_ARRAYSIZE(panelPositions); ++i) LayoutPanel(static_cast<PositionPanel>(i), width, height);
    LayoutHelp(width, height);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CloseLayout();
}
}
