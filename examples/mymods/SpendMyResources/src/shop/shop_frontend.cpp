#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include "app.h"
#include "canvas.h"
#include "icons.h"
#include "local_stats.h"
#include "sounds.h"
#include "shop_frontend.h"
#include "../currency/currency_ledger.cpp"
#include "../config/prices.cpp"
#include "../config/upgrade_config.cpp"

namespace editorapp
{
static const HfApi* api;
static HfMod* mod;
static void (*showCheckout)(bool);
enum class CheckoutPanel { Destroyer, Helldiver, Purchase };
static CheckoutPanel checkoutPanel;
static float tabIndicator;
static bool shopOpen;
static bool exchangeOpen;
static bool loadoutOpen;
static size_t destroyerSelection;
static uint32_t destroyerExtra;
static SmrHelldiverPerk helldiverSelection = SMR_PERK_PRIMARY_TRADEOFF;
static void (*closeWindow)();
enum class PositionPanel { Checkout, Exchange, LoadoutCost };
struct PanelPosition
{
    ImVec2 position;
    ImVec2 current;
    bool hasPosition;
    bool pinned = true;
    bool initialized;
};
static PanelPosition panelPositions[3];
static std::wstring panelPositionsPath;
static bool panelPositionsLoaded;

static constexpr ImU32 kAccent = IM_COL32(250, 219, 67, 255);
static constexpr ImU32 kAccentText = IM_COL32(255, 235, 147, 255);
static constexpr ImU32 kWarning = IM_COL32(251, 161, 119, 255);
static constexpr ImU32 kSurface = IM_COL32(32, 38, 46, 255);
static constexpr ImU32 kSelectedSurface = IM_COL32(63, 57, 34, 255);
static constexpr ImU32 kBorder = IM_COL32(82, 93, 104, 255);
static constexpr struct {
    ImGuiCol slot;
    ImU32 color;
} kShopPalette[] = {
    {ImGuiCol_Text, IM_COL32(238, 240, 238, 255)},
    {ImGuiCol_TextDisabled, IM_COL32(164, 175, 185, 255)},
    {ImGuiCol_WindowBg, IM_COL32(15, 19, 25, 239)},
    {ImGuiCol_ChildBg, IM_COL32(10, 13, 18, 90)},
    {ImGuiCol_PopupBg, IM_COL32(22, 27, 33, 250)},
    {ImGuiCol_Border, kBorder},
    {ImGuiCol_FrameBg, IM_COL32(23, 28, 34, 255)},
    {ImGuiCol_FrameBgHovered, kSurface},
    {ImGuiCol_FrameBgActive, kSelectedSurface},
    {ImGuiCol_TitleBg, kSurface},
    {ImGuiCol_TitleBgActive, kSurface},
    {ImGuiCol_TitleBgCollapsed, kSurface},
    {ImGuiCol_Button, kSurface},
    {ImGuiCol_ButtonHovered, kSurface},
    {ImGuiCol_ButtonActive, kSelectedSurface},
    {ImGuiCol_Header, kSelectedSurface},
    {ImGuiCol_HeaderHovered, IM_COL32(51, 59, 68, 255)},
    {ImGuiCol_HeaderActive, kSelectedSurface},
    {ImGuiCol_Separator, kBorder},
    {ImGuiCol_ScrollbarBg, IM_COL32(9, 13, 18, 130)},
    {ImGuiCol_ScrollbarGrab, IM_COL32(89, 102, 112, 255)},
    {ImGuiCol_ScrollbarGrabHovered, IM_COL32(118, 130, 139, 255)},
    {ImGuiCol_ScrollbarGrabActive, kAccentText},
    {ImGuiCol_TextSelectedBg, IM_COL32(154, 128, 43, 130)},
    {ImGuiCol_NavCursor, kAccentText},
};

struct ShopTheme
{
    ShopTheme()
    {
        for (const auto& entry : kShopPalette) ImGui::PushStyleColor(entry.slot, entry.color);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    }
    ~ShopTheme()
    {
        ImGui::PopStyleVar(5);
        ImGui::PopStyleColor(IM_ARRAYSIZE(kShopPalette));
    }
};

static float AnimateHighlight(float progress, bool on, float seconds)
{
    const float step = ImGui::GetIO().DeltaTime / seconds;
    return std::clamp(progress + (on ? step : -step), 0.0f, 1.0f);
}

static void HoverHighlight(const char* label, bool enabled = true)
{
    ImGui::PushID(label);
    const ImGuiID key = ImGui::GetID("##hover-fade");
    ImGui::PopID();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const float previous = storage->GetFloat(key);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
    const float fade = enabled ? AnimateHighlight(previous, hovered, 0.12f) : 0.0f;
    if (fade != previous) storage->SetFloat(key, fade);
    if (fade <= 0 || !ImGui::IsItemVisible()) return;
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::GetColorU32(kAccent, fade * 0.10f));
    draw->AddRect(min, max, ImGui::GetColorU32(kAccent, fade * 0.65f));
}

static bool ShopButton(const char* label, bool enabled, bool primary = false)
{
    ImGui::BeginDisabled(!enabled);
    if (primary)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentText);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(143, 125, 60, 255));
    }
    const bool pressed = ImGui::Button(label);
    if (primary) ImGui::PopStyleColor(2);
    HoverHighlight(label, enabled);
    ImGui::EndDisabled();
    return enabled && pressed;
}

static bool UpgradeTile(const char* label, float side, bool selected)
{
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, kSelectedSurface);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSelectedSurface);
    }
    const bool pressed = ImGui::Button(label, ImVec2(side, side));
    if (selected) ImGui::PopStyleColor(2);
    HoverHighlight(label);
    if ((selected || pressed) && ImGui::IsItemVisible())
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(kAccent), 0.0f, 0, 1.5f);
    return pressed;
}

enum class UpgradeIcon { Orbital, Eagle, Defensive, Support, Slot };
static constexpr ImU32 kOffensiveTint = IM_COL32(226, 94, 74, 255);
static constexpr ImU32 kSupportTint = IM_COL32(74, 172, 232, 255);
static constexpr ImU32 kDefensiveTint = IM_COL32(122, 188, 94, 255);
static constexpr struct {
    const char* name;
    UpgradeIcon icon;
    const char* sprite;
    ImU32 tint;
    SmrUpgrade upgrade;
} kDestroyerUpgrades[] = {
    {"Orbital Upgrade",
        UpgradeIcon::Orbital, "Orbital Precision Strike", kOffensiveTint, SMR_UPGRADE_ORBITAL},
    {"Eagle Upgrade",
        UpgradeIcon::Eagle, "Eagle Airstrike", kOffensiveTint, SMR_UPGRADE_EAGLE},
    {"Defensive Upgrade",
        UpgradeIcon::Defensive, "A/G-16 Gatling Sentry", kDefensiveTint, SMR_UPGRADE_DEFENSIVE},
    {"Support Upgrade",
        UpgradeIcon::Support, "B-1 Supply Pack", kSupportTint, SMR_UPGRADE_SUPPORT},
    {"Extra Stratagem Slot",
        UpgradeIcon::Slot, nullptr, 0, SMR_UPGRADE_EXTRA_SLOT},
};
static constexpr size_t kUpgradeCurrencies[] = {REQUISITION_SLIPS, FORGE_CREDITS, COMMON_SAMPLES, RARE_SAMPLES, SUPER_SAMPLES};
static constexpr const char* kCurrencyIcons[CURRENCY_COUNT] = {
    "Requisition_Slip.png", "Common_Sample_Icon.png", "Rare_Sample_Icon.png",
    "Super_Sample_Icon.png", "Forge_Credit.png",
};

static size_t PositionIndex(PositionPanel panel) { return static_cast<size_t>(panel); }
static const char* PositionName(PositionPanel panel)
{
    static constexpr const char* names[] = {"checkout", "exchange", "loadout_cost"};
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

static void LoadPanelPositions()
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

static void SavePanelPositions()
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

static void PreparePanel(PositionPanel panel, float width, float height, ImVec2 fallback, ImVec2 pivot)
{
    LoadPanelPositions();
    PanelPosition& state = panelPositions[PositionIndex(panel)];
    if (state.initialized) return;
    state.initialized = true;
    if (state.hasPosition)
    {
        ImVec2 position = state.position;
        position.x = std::clamp(position.x, 0.0f, std::max(0.0f, width - 80.0f));
        position.y = std::clamp(position.y, 0.0f, std::max(0.0f, height - 40.0f));
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    }
    else ImGui::SetNextWindowPos(fallback, ImGuiCond_Always, pivot);
}

static void TrackPanel(PositionPanel panel)
{
    panelPositions[PositionIndex(panel)].current = ImGui::GetWindowPos();
}

static void PositionToggle(PositionPanel panel, ImVec2 position)
{
    PanelPosition& state = panelPositions[PositionIndex(panel)];
    char label[48];
    snprintf(label, sizeof label, "%s##%s-position", state.pinned ? "Unpin" : "Pin", PositionName(panel));
    if (ImGui::SmallButton(label))
    {
        state.pinned = !state.pinned;
        if (state.pinned)
        {
            state.position = position;
            state.hasPosition = true;
            SavePanelPositions();
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(state.pinned ? "Unpin to move this panel" : "Pin to save this position");
}

static std::string Amount(unsigned amount, const char* name)
{
    std::string text = std::to_string(amount) + " " + name;
    if (amount == 1 && text.back() == 's') text.pop_back();
    return text;
}

static ImU32 CurrencyColor(size_t currency)
{
    static constexpr ImU32 colors[CURRENCY_COUNT] = {
        IM_COL32(226, 190, 96, 255), IM_COL32(137, 194, 109, 255), IM_COL32(101, 172, 226, 255),
        IM_COL32(221, 130, 102, 255), IM_COL32(180, 145, 224, 255),
    };
    return colors[currency];
}

static void DrawIconLayer(const icons::Icon& icon, ImU32 color, ImVec2 center, float size);

static void DrawCurrencyGraphic(size_t currency, ImVec2 min, float side)
{
    const float scale = side / 16.0f;
    const auto point = [=](float x, float y) { return ImVec2(min.x + x * scale, min.y + y * scale); };
    const icons::Icon* icon = icons::Texture() ? icons::Find(icons::MOD, kCurrencyIcons[currency]) : nullptr;
    if (icon)
    {
        DrawIconLayer(*icon, IM_COL32_WHITE, point(8, 8), side);
        return;
    }
    const ImU32 color = ImGui::GetColorU32(CurrencyColor(currency));
    const ImU32 outline = ImGui::GetColorU32(IM_COL32(20, 24, 29, 255));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (currency == REQUISITION_SLIPS)
    {
        draw->AddRectFilled(point(1, 3), point(15, 13), color, 2.0f * scale);
        draw->AddRect(point(1, 3), point(15, 13), outline, 2.0f * scale, 0, scale);
        draw->AddLine(point(4, 7), point(12, 7), outline, scale);
        draw->AddLine(point(4, 10), point(12, 10), outline, scale);
    }
    else if (currency == FORGE_CREDITS)
    {
        draw->AddCircleFilled(point(8, 8), 7.0f * scale, color);
        draw->AddCircle(point(8, 8), 7.0f * scale, outline, 0, scale);
        draw->AddNgonFilled(point(8, 8), 3.0f * scale, outline, 4);
    }
    else
    {
        draw->AddRectFilled(point(5, 1), point(11, 4), outline);
        draw->AddRectFilled(point(3, 4), point(13, 15), color, 3.0f * scale);
        draw->AddRect(point(3, 4), point(13, 15), outline, 3.0f * scale, 0, scale);
        draw->AddLine(point(5, 10), point(11, 10), outline, scale);
    }
}

static void DrawCurrencyIcon(size_t currency, const char* detail = "wallet balance / cost")
{
    constexpr float side = 16.0f;
    DrawCurrencyGraphic(currency, ImGui::GetCursorScreenPos(), side);
    ImGui::Dummy(ImVec2(side, side));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s: %s", kCurrencies[currency].name, detail);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

static void DrawCurrencyAmount(size_t currency, uint64_t amount)
{
    DrawCurrencyIcon(currency);
    ImGui::Text("%llu", static_cast<unsigned long long>(amount));
}

static void UpgradeCostValues(const unsigned price[CURRENCY_COUNT], const unsigned balance[CURRENCY_COUNT])
{
    bool first = true;
    for (size_t i : kUpgradeCurrencies)
    {
        if (!price[i]) continue;
        if (!first) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
        DrawCurrencyIcon(i);
        if (balance[i] < price[i]) ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
        ImGui::Text("%u / %u", balance[i], price[i]);
        if (balance[i] < price[i]) ImGui::PopStyleColor();
        first = false;
    }
}

static void DescribeUpgrade(SmrUpgrade upgrade, const SmrUpgradeTuning& tuning)
{
    if (upgrade == SMR_UPGRADE_ORBITAL)
        ImGui::TextWrapped("Equipped orbital cooldowns: %+.6g%%.", tuning.cooldown_percent);
    else if (upgrade == SMR_UPGRADE_EAGLE)
        ImGui::TextWrapped("Eagle rearm: %.6g s. Call-in time: %+.6g s. Cooldown: %.6g s.",
            tuning.rearm_seconds, tuning.call_in_extra_seconds, tuning.cooldown_seconds);
    else if (upgrade == SMR_UPGRADE_EXTRA_SLOT)
    {
        ImGui::TextWrapped("One additional owned Stratagem.");
        ImGui::TextWrapped("All other stratagem equipped cooldowns: %+.6g%%.",
            tuning.other_cooldown_percent);
        return;
    }
    else
        ImGui::TextWrapped("Equipped %s stratagems: %+.6g%% to cooldowns and unlimited uses.",
            upgrade == SMR_UPGRADE_DEFENSIVE ? "defensive" : "support", tuning.cooldown_percent);
    ImGui::TextWrapped("Other equipped cooldowns%s: %+.6g%%.",
        upgrade == SMR_UPGRADE_EAGLE ? "" : " and Eagle rearm", tuning.other_cooldown_percent);
}

static const char* UpgradeName(SmrUpgrade upgrade)
{
    for (const auto& choice : kDestroyerUpgrades)
        if (choice.upgrade == upgrade) return choice.name;
    return "Destroyer Upgrade";
}

static void CloseCheckout(bool ready = false)
{
    showCheckout(false);
    SmrFinishCheckout(ready);
    shopOpen = false;
}

void Init(const Host& host)
{
    api = host.api;
    mod = host.mod;
    showCheckout = host.modal;
    closeWindow = host.close;
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
    exchangeOpen = true;
}
void Closed() { exchangeOpen = false; }
void Tick()
{
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
        shopOpen = true;
        checkoutPanel = CheckoutPanel::Destroyer;
        tabIndicator = 0.0f;
        destroyerSelection = 0;
        destroyerExtra = 0;
        helldiverSelection = SMR_PERK_PRIMARY_TRADEOFF;
        showCheckout(true);
    }
    else if (shopOpen && SmrCheckoutComplete()) CloseCheckout(true);
}

static void DrawIconLayer(const icons::Icon& icon, ImU32 color, ImVec2 center, float size)
{
    const float fit = size / (icon.width > icon.height ? icon.width : icon.height);
    const float x = icon.width * fit / 2, y = icon.height * fit / 2;
    ImGui::GetWindowDrawList()->AddImage(icons::Texture(),
        ImVec2(center.x - x, center.y - y), ImVec2(center.x + x, center.y + y),
        icon.uv0, icon.uv1, ImGui::GetColorU32(color));
}

static bool DrawGameStratagemIcon(const char* sprite, ImU32 tint, ImVec2 center, float size)
{
    if (!sprite || !icons::Texture()) return false;
    const icons::Icon* layer = icons::Find(icons::STRATAGEM, sprite);
    const icons::Icon* white = icons::Find(icons::STRATAGEM_WHITE, sprite);
    if (!layer || !white) return false;
    DrawIconLayer(*layer, tint, center, size);
    DrawIconLayer(*white, IM_COL32(255, 255, 238, 255), center, size);
    return true;
}

static void DrawUpgradeIcon(UpgradeIcon icon, ImVec2 center, float scale)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float stroke = 2 * scale;
    const auto p = [=](float x, float y) { return ImVec2(center.x + x * scale, center.y + y * scale); };
    const auto line = [&](float x1, float y1, float x2, float y2) { draw->AddLine(p(x1, y1), p(x2, y2), color, stroke); };
    switch (icon)
    {
    case UpgradeIcon::Orbital:
        draw->AddCircle(center, 10 * scale, color, 24, stroke);
        draw->AddCircleFilled(center, 2 * scale, color);
        line(-16, 0, -6, 0); line(6, 0, 16, 0);
        line(0, -16, 0, -6); line(0, 6, 0, 16);
        break;
    case UpgradeIcon::Eagle:
        draw->AddTriangleFilled(p(0, -15), p(16, 10), p(0, 4), color);
        draw->AddTriangleFilled(p(0, -15), p(0, 4), p(-16, 10), color);
        line(0, 4, 0, 15);
        break;
    case UpgradeIcon::Defensive:
    {
        const ImVec2 shield[] = {p(-12, -9), p(0, -14), p(12, -9), p(10, 6), p(0, 14), p(-10, 6)};
        draw->AddPolyline(shield, IM_ARRAYSIZE(shield), color, ImDrawFlags_Closed, stroke);
        break;
    }
    case UpgradeIcon::Support:
        draw->AddRect(p(-13, -10), p(13, 12), color, 2 * scale, 0, stroke);
        line(-5, -10, -5, -15); line(-5, -15, 5, -15); line(5, -15, 5, -10);
        line(-6, 1, 6, 1); line(0, -5, 0, 7);
        break;
    case UpgradeIcon::Slot:
        draw->AddRect(p(-13, -13), p(13, 13), color, 2 * scale, 0, stroke);
        line(-7, 0, 7, 0); line(0, -7, 0, 7);
        break;
    }
}

static size_t DestroyerPicker(const SmrDestroyerStatus& upgrade, bool pending)
{
    size_t preview = destroyerSelection;
    size_t hovered = IM_ARRAYSIZE(kDestroyerUpgrades);
    ImGui::TextUnformatted("Choose an upgrade");
    constexpr int count = IM_ARRAYSIZE(kDestroyerUpgrades) + 1;
    const float preferred = ImGui::GetFrameHeight() * 2.75f;
    const float fit = (ImGui::GetContentRegionAvail().x - (count - 1) * ImGui::GetStyle().ItemSpacing.x) / count;
    const float side = preferred < fit ? preferred : fit;
    for (size_t i = 0; i < IM_ARRAYSIZE(kDestroyerUpgrades); ++i)
    {
        if (i) ImGui::SameLine();
        char label[48];
        snprintf(label, sizeof label, "##%s", kDestroyerUpgrades[i].name);
        const auto& choice = kDestroyerUpgrades[i];
        const bool enabled = upgrade.available && !upgrade.purchased && !pending && SmrGetUpgradeTuning(choice.upgrade);
        ImGui::BeginDisabled(!enabled);
        if (UpgradeTile(label, side, choice.upgrade == upgrade.selected) && enabled)
        {
            const SmrUpgrade selected = choice.upgrade == upgrade.selected ? SMR_UPGRADE_NONE : choice.upgrade;
            const uint32_t extra = selected == SMR_UPGRADE_EXTRA_SLOT ? destroyerExtra : 0;
            if (SmrSelectUpgrade(mod, selected == SMR_UPGRADE_EXTRA_SLOT && !extra ? SMR_UPGRADE_NONE : selected, extra))
            {
                destroyerSelection = i;
                preview = i;
                if (selected != SMR_UPGRADE_EXTRA_SLOT) destroyerExtra = 0;
            }
        }
        ImGui::EndDisabled();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
        if (!DrawGameStratagemIcon(choice.sprite, choice.tint, center, side * 0.76f))
            DrawUpgradeIcon(choice.icon, center, side / 52);
        if (ImGui::IsItemFocused()) preview = i;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) hovered = i;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", choice.name);
    }
    ImGui::SameLine();
    const bool canRemove = !pending && !upgrade.purchased && (upgrade.selected || destroyerExtra || destroyerSelection);
    ImGui::BeginDisabled(!canRemove);
    if (UpgradeTile(" X ##Clear destroyer", side, false) && canRemove &&
        SmrSelectUpgrade(mod, SMR_UPGRADE_NONE, 0))
    {
        destroyerSelection = 0;
        destroyerExtra = 0;
        preview = 0;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Remove Destroyer upgrade");
    ImGui::Separator();
    return hovered < IM_ARRAYSIZE(kDestroyerUpgrades) ? hovered : preview;
}

static void ExtraTile(const SmrExtraChoice& choice, ImU32 tint, float side, bool enabled)
{
    ImGui::PushID(static_cast<int>(choice.type));
    ImGui::BeginDisabled(!enabled);
    if (UpgradeTile("##Stratagem", side, choice.type == destroyerExtra) && enabled &&
        SmrSelectUpgrade(mod, SMR_UPGRADE_EXTRA_SLOT, choice.type))
        destroyerExtra = choice.type;
    ImGui::EndDisabled();
    if (ImGui::IsItemVisible())
    {
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (!DrawGameStratagemIcon(choice.name, tint, center, side * 0.84f))
        {
            const char* caption = choice.name;
            if (std::strncmp(caption, "Eagle ", 6) == 0) caption += 6;
            else if (std::strncmp(caption, "Orbital ", 8) == 0) caption += 8;
            const ImVec4 clip(min.x + 3, min.y + 3, max.x - 3, max.y - 3);
            const float wrap = clip.z - clip.x;
            const ImVec2 text = ImGui::CalcTextSize(caption, nullptr, false, wrap);
            const float y = center.y - text.y / 2;
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(center.x - text.x / 2, y > clip.y ? y : clip.y),
                ImGui::GetColorU32(tint), caption, nullptr, wrap, &clip);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", choice.name);
    ImGui::PopID();
}

static bool ExtraPicker(bool enabled)
{
    SmrExtraChoice choices[SMR_MAX_EXTRA_CHOICES];
    const size_t count = SmrExtraChoices(choices, IM_ARRAYSIZE(choices));
    if (!count)
    {
        ImGui::TextDisabled("No eligible extras. Check your loadout and the mod log.");
        return false;
    }
    static constexpr struct { uint32_t category; const char* name; ImU32 tint; } groups[] = {
        {HF_CATEGORY_OFFENSIVE, "Offensive", kOffensiveTint},
        {HF_CATEGORY_SUPPLY, "Support", kSupportTint},
        {HF_CATEGORY_DEFENSIVE, "Defensive", kDefensiveTint},
        {HF_CATEGORY_SPECIAL, "Special", IM_COL32(214, 186, 110, 255)},
    };
    const ImGuiStyle& style = ImGui::GetStyle();
    const float side = ImGui::GetFrameHeight() * 2.5f, gap = style.ItemSpacing.x;
    const float preferred = 8 * side + 7 * gap + 2 * style.WindowPadding.x + style.ScrollbarSize;
    const float limit = ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("Choose an extra stratagem");
    const float remaining = ImGui::GetContentRegionAvail().y;
    const float gridHeight = remaining < 100 ? 100 : remaining;
    if (ImGui::BeginChild("Extra stratagem grid", ImVec2(preferred < limit ? preferred : limit, gridHeight),
        ImGuiChildFlags_Borders))
    {
        const int fit = static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (side + gap));
        const int columns = fit > 0 ? fit : 1;
        for (const auto& group : groups)
        {
            int column = 0;
            for (size_t i = 0; i < count; ++i)
            {
                const auto& choice = choices[i];
                if (choice.category != group.category) continue;
                if (!column)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, group.tint);
                    ImGui::SeparatorText(group.name);
                    ImGui::PopStyleColor();
                }
                if (column++ % columns) ImGui::SameLine();
                ExtraTile(choice, group.tint, side, enabled);
            }
        }
    }
    ImGui::EndChild();
    for (size_t i = 0; i < count; ++i)
        if (choices[i].type == destroyerExtra)
        {
            ImGui::TextWrapped("Selected: %s", choices[i].name);
            return true;
        }
    ImGui::TextDisabled("Selected: None");
    return false;
}

static void DestroyerPanel(SmrDestroyerStatus upgrade, bool pending)
{
    if (upgrade.purchased)
    {
        ImGui::TextWrapped("%s already purchased for your next mission.", UpgradeName(upgrade.purchased));
        if (upgrade.purchased == SMR_UPGRADE_EXTRA_SLOT)
            if (const char* name = SmrExtraName(upgrade.extra)) ImGui::TextWrapped("Extra stratagem: %s", name);
    }
    else if (upgrade.selected)
        ImGui::TextWrapped("Selected for purchase: %s", UpgradeName(upgrade.selected));
    else ImGui::TextDisabled("No upgrade selected");
    ImGui::Separator();
    const size_t preview = DestroyerPicker(upgrade, pending);
    upgrade = SmrDestroyer(mod);
    if (!ImGui::BeginChild("Destroyer description", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        ImGui::EndChild();
        return;
    }
    const auto& choice = kDestroyerUpgrades[preview];
    const SmrUpgradeTuning* tuning = SmrGetUpgradeTuning(choice.upgrade);
    const bool extraSlot = choice.upgrade == SMR_UPGRADE_EXTRA_SLOT;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", choice.name);
    if (tuning) DescribeUpgrade(choice.upgrade, *tuning);
    else if (extraSlot)
        ImGui::TextWrapped("Extra slot disabled: check the extra_slot section in destroyer-upgrades.json and the mod log.");
    else ImGui::TextWrapped("Upgrades disabled: check destroyer-upgrades.json and the mod log.");
    ImGui::Separator();
    if (tuning && !upgrade.purchased)
    {
        if (!upgrade.available) ImGui::TextDisabled("Upgrade selection unavailable.");
        else
        {
            ImGui::TextUnformatted("Cost:");
            ImGui::SameLine();
            UpgradeCostValues(tuning->price, upgrade.balance);
            if (extraSlot)
            {
                ImGui::TextWrapped("Choose an extra stratagem to select this upgrade.");
                ExtraPicker(!pending);
            }
        }
    }
    ImGui::EndChild();
}

static void DestroyerActions(bool pending)
{
    const SmrDestroyerStatus upgrade = SmrDestroyer(mod);
    if (ShopButton("Review purchase", upgrade.selected && !pending, true))
        checkoutPanel = CheckoutPanel::Purchase;
}

static constexpr const char* kHelldiverTiers[SMR_TIER_COUNT] = {"Plus", "Elite", "Super"};
static constexpr const char* kPerkNames[SMR_PERK_COUNT] = {
    "None", "Primary Ammo Trade-off", "Secondary Ammo Trade-off", "Light Kit", "Armored", "Sprinter", "Endurance",
    "Auto-Stim", "Field Medic",
};
static constexpr struct {
    icons::Kind kind;
    const char* sprite;
    const char* abbreviation;
} kPerkIcons[SMR_PERK_COUNT] = {
    {icons::WEAPON, nullptr, ""},
    {icons::WEAPON, "P-2 Peacemaker", "PA"},
    {icons::WEAPON, "AR-23 Liberator", "SA"},
    {icons::PASSIVE, nullptr, "LK"},
    {icons::PASSIVE, "EXTRA PADDING", "AR"},
    {icons::BOOSTER, "Dead Sprint", "SP"},
    {icons::BOOSTER, "Stamina Enhancement", "EN"},
    {icons::BOOSTER, "Experimental Infusion", "AS"},
    {icons::PASSIVE, "MED-KIT", "FM"},
};
static const char* PerkName(SmrHelldiverPerk perk)
{
    return perk > SMR_PERK_NONE && perk < SMR_PERK_COUNT ? kPerkNames[perk] : "Helldiver Upgrade";
}

static void DescribePerk(SmrHelldiverPerk perk, const SmrHelldiverTuning& tuning)
{
    if (perk == SMR_PERK_AUTO_STIM)
        ImGui::BulletText("Auto-heal at %.6g%% health or lower, once every %.6g s.", tuning.health_percent,
            tuning.cooldown_seconds);
    if (perk == SMR_PERK_FIELD_MEDIC) ImGui::BulletText("Injured limbs heal after %.6g s.", tuning.injury_seconds);
    const struct { const char* label; double value; } effects[] = {
        {"Primary magazines", tuning.primary_ammo_percent}, {"Secondary magazines", tuning.secondary_ammo_percent},
        {"Throwables", tuning.throwables_percent},
        {"Armor rating", tuning.armor_percent}, {"Sprint speed", tuning.sprint_percent},
        {"Stamina", tuning.stamina_percent}, {"Movement speed", tuning.movement_percent},
    };
    for (const auto& effect : effects)
        if (effect.value) ImGui::BulletText("%s: %+.6g%%", effect.label, effect.value);
}

static void DrawPerkIcon(SmrHelldiverPerk perk, float side)
{
    if (!ImGui::IsItemVisible()) return;
    const auto& choice = kPerkIcons[perk];
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
    const icons::Icon* icon = choice.sprite && icons::Texture() ? icons::Find(choice.kind, choice.sprite) : nullptr;
    if (icon)
    {
        DrawIconLayer(*icon, choice.kind == icons::BOOSTER ? IM_COL32_WHITE : kAccentText, center, side * 0.76f);
        return;
    }
    const ImVec2 text = ImGui::CalcTextSize(choice.abbreviation);
    ImGui::GetWindowDrawList()->AddText(ImVec2(center.x - text.x / 2, center.y - text.y / 2),
        ImGui::GetColorU32(ImGuiCol_Text), choice.abbreviation);
}

static SmrHelldiverPerk HelldiverPicker(const SmrHelldiverStatus& status, bool pending)
{
    SmrHelldiverPerk preview = helldiverSelection;
    SmrHelldiverPerk hovered = SMR_PERK_NONE;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float labelWidth = ImGui::CalcTextSize("Super").x + style.ItemSpacing.x * 2;
    const float fit = (ImGui::GetContentRegionAvail().x - labelWidth - 3 * style.ItemSpacing.x -
        4 * style.CellPadding.x) / 4;
    const float side = std::min(ImGui::GetFrameHeight() * 2.75f, fit);
    if (!ImGui::BeginTable("Helldiver tiers", 2, ImGuiTableFlags_SizingFixedFit)) return preview;
    ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("Upgrades", ImGuiTableColumnFlags_WidthStretch);
    for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
    {
        ImGui::TableNextRow(0, side + style.ItemSpacing.y);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(tier);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (side - ImGui::GetTextLineHeight()) / 2);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", kHelldiverTiers[tier]);
        ImGui::TableSetColumnIndex(1);
        int count = 0;
        for (int perk = SMR_PERK_NONE + 1; perk < SMR_PERK_COUNT; ++perk)
        {
            const auto choice = static_cast<SmrHelldiverPerk>(perk);
            if (SmrPerkTier(choice) != tier) continue;
            if (count++) ImGui::SameLine();
            const bool selected = status.selected[tier] == choice || status.purchased[tier] == choice;
            const bool enabled = status.available[tier] && !status.purchased[tier] && !pending && SmrGetHelldiverTuning(choice);
            ImGui::PushID(perk);
            ImGui::BeginDisabled(!enabled);
            if (UpgradeTile("##Perk", side, selected) && enabled &&
                SmrSelectHelldiver(mod, static_cast<SmrHelldiverTier>(tier),
                    status.selected[tier] == choice ? SMR_PERK_NONE : choice))
                helldiverSelection = preview = choice;
            DrawPerkIcon(choice, side);
            ImGui::EndDisabled();
            if (ImGui::IsItemFocused()) preview = choice;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) hovered = choice;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", PerkName(choice));
            ImGui::PopID();
        }
        if (count < 3)
        {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(side, side));
        }
        ImGui::SameLine();
        const bool canRemove = status.selected[tier] && !status.purchased[tier] && !pending;
        ImGui::BeginDisabled(!canRemove);
        if (UpgradeTile(" X ##Clear helldiver", side, false) && canRemove)
            SmrSelectHelldiver(mod, static_cast<SmrHelldiverTier>(tier), SMR_PERK_NONE);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Remove %s upgrade", kHelldiverTiers[tier]);
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::Separator();
    return hovered != SMR_PERK_NONE ? hovered : preview;
}

static void HelldiverPanel(const SmrHelldiverStatus& status, bool pending)
{
    ImGui::TextWrapped("Pick one upgrade from each tier. Tiers stack for one mission.");
    const SmrHelldiverPerk preview = HelldiverPicker(status, pending);
    const SmrHelldiverStatus current = SmrHelldiver(mod);
    const SmrHelldiverTier tier = SmrPerkTier(preview);
    const SmrHelldiverTuning* tuning = SmrGetHelldiverTuning(preview);
    if (ImGui::BeginChild("Helldiver description", ImVec2(0, 0),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s: %s", kHelldiverTiers[tier], PerkName(preview));
        if (tuning) DescribePerk(preview, *tuning);
        else ImGui::TextWrapped("Helldiver upgrades disabled: check helldiver-upgrades.json and the mod log.");
        if (current.purchased[tier])
            ImGui::TextWrapped("%s already purchased for your next mission.", PerkName(current.purchased[tier]));
        else if (current.selected[tier]) ImGui::TextWrapped("Selected for purchase: %s", PerkName(current.selected[tier]));
        else ImGui::TextDisabled("No %s upgrade selected", kHelldiverTiers[tier]);
        ImGui::Separator();
        if (!current.available[tier] && !current.purchased[tier]) ImGui::TextDisabled("Upgrade selection unavailable.");
        else if (tuning && !current.purchased[tier])
        {
            ImGui::TextUnformatted("Cost:");
            ImGui::SameLine();
            UpgradeCostValues(tuning->price, current.balance);
        }
    }
    ImGui::EndChild();
}

static void HelldiverActions(bool pending)
{
    const SmrHelldiverStatus status = SmrHelldiver(mod);
    bool any = false;
    for (SmrHelldiverPerk perk : status.selected) any = any || perk != SMR_PERK_NONE;
    if (ShopButton("Review purchase", any && !pending, true)) checkoutPanel = CheckoutPanel::Purchase;
}

static void PurchasePanel(uint64_t requisition, const unsigned price[CURRENCY_COUNT])
{
    const SmrDestroyerStatus upgrade = SmrDestroyer(mod);
    const SmrHelldiverStatus helldiver = SmrHelldiver(mod);
    bool perks = false;
    for (SmrHelldiverPerk perk : helldiver.selected) perks = perks || perk != SMR_PERK_NONE;
    unsigned balance = 0;
    if (SmrBalance(&balance))
    {
        ImGui::TextUnformatted("Wallet balance:");
        ImGui::SameLine();
        DrawCurrencyAmount(REQUISITION_SLIPS, balance);
        if (balance < requisition)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarning), "Not enough requisition for this purchase.");
    }
    else ImGui::TextDisabled("Requisition slips: unavailable");
    ImGui::Separator();
    SmrItem item;
    bool listed = false;
    for (size_t i = 0; SmrShopItem(i, &item); ++i)
    {
        if (!item.selected) continue;
        if (!listed) ImGui::TextUnformatted("Restricted item access");
        listed = true;
        if (item.access) ImGui::TextWrapped("%s - Access ready", item.name);
        else
        {
            ImGui::TextWrapped("%s -", item.name);
            ImGui::SameLine();
            DrawCurrencyAmount(REQUISITION_SLIPS, item.price);
        }
    }
    if (upgrade.selected || perks)
    {
        ImGui::Separator();
        if (upgrade.selected) ImGui::TextUnformatted(UpgradeName(upgrade.selected));
        if (upgrade.selected == SMR_UPGRADE_EXTRA_SLOT)
            if (const char* name = SmrExtraName(upgrade.extra)) ImGui::TextWrapped("Extra stratagem: %s", name);
        for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
            if (helldiver.selected[tier])
                ImGui::TextWrapped("%s: %s", kHelldiverTiers[tier], PerkName(helldiver.selected[tier]));
        ImGui::TextUnformatted("Cost:");
        ImGui::SameLine();
        UpgradeCostValues(price, upgrade.balance);
    }
}

static std::string PurchaseTotal(uint64_t requisition, const unsigned price[CURRENCY_COUNT])
{
    bool upgradeDue = false;
    for (size_t i : kUpgradeCurrencies) upgradeDue = upgradeDue || (i != REQUISITION_SLIPS && price[i]);
    std::string totalDue;
    if (requisition || !upgradeDue) totalDue = std::to_string(requisition) + " Req";
    for (size_t i : kUpgradeCurrencies)
        if (i != REQUISITION_SLIPS && price[i])
            totalDue += (totalDue.empty() ? "" : " + ") + Amount(price[i], kCurrencies[i].name);
    return "Total due: " + totalDue;
}

static void PurchaseActions(uint64_t requisition, const unsigned price[CURRENCY_COUNT], bool hasCharges,
    SmrCheckoutAction action, int& pending)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentText);
    ImGui::TextUnformatted("Total due:");
    bool upgradeDue = false;
    for (size_t i : kUpgradeCurrencies)
        if (i != REQUISITION_SLIPS && price[i]) upgradeDue = true;
    if (requisition || !upgradeDue)
    {
        ImGui::SameLine();
        DrawCurrencyAmount(REQUISITION_SLIPS, requisition);
    }
    for (size_t i : kUpgradeCurrencies)
        if (i != REQUISITION_SLIPS && price[i])
        {
            ImGui::SameLine();
            DrawCurrencyAmount(i, price[i]);
        }
    ImGui::PopStyleColor();
    const char* label = pending ? "Processing..." : hasCharges ? "Purchase" : "Continue to Ready";
    if (ShopButton(label, action != SMR_CHECKOUT_BLOCKED, true))
        pending = SmrConfirmCheckout(mod, action) != 0;
}

static void CheckoutTabs(bool pending)
{
    static constexpr struct { const char* label; CheckoutPanel panel; } tabs[] = {
        {"Destroyer", CheckoutPanel::Destroyer}, {"Helldiver", CheckoutPanel::Helldiver},
        {"Purchase##tab", CheckoutPanel::Purchase}
    };
    constexpr size_t count = IM_ARRAYSIZE(tabs);
    ImVec2 min[count], max[count];
    size_t current = 0;
    ImGui::BeginDisabled(pending);
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
    for (size_t i = 0; i < IM_ARRAYSIZE(tabs); ++i)
    {
        if (i) ImGui::SameLine();
        const auto& tab = tabs[i];
        const bool active = checkoutPanel == tab.panel;
        const ImU32 surface = active ? kSurface : 0u;
        ImGui::PushStyleColor(ImGuiCol_Header, surface);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, surface);
        ImGui::PushStyleColor(ImGuiCol_Text, active ? ImGui::ColorConvertU32ToFloat4(kAccentText) :
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(tab.label, active, 0, ImVec2(100, ImGui::GetFrameHeight())) && !pending)
            checkoutPanel = tab.panel;
        ImGui::PopStyleColor(3);
        min[i] = ImGui::GetItemRectMin();
        max[i] = ImGui::GetItemRectMax();
        HoverHighlight(tab.label, !pending);
    }
    ImGui::PopStyleVar();
    for (size_t i = 0; i < count; ++i)
        if (checkoutPanel == tabs[i].panel) current = i;
    /* tabIndicator is the underline position in tabs; a full sweep takes 0.16 s. */
    const float step = ImGui::GetIO().DeltaTime / 0.16f * (count - 1), target = static_cast<float>(current);
    tabIndicator = tabIndicator < target ? std::min(tabIndicator + step, target) : std::max(tabIndicator - step, target);
    const size_t from = std::min(static_cast<size_t>(tabIndicator), count - 2);
    const float t = tabIndicator - from, slide = t * t * (3.0f - 2.0f * t);
    const float left = min[from].x + (min[from + 1].x - min[from].x) * slide + 3.0f;
    const float right = max[from].x + (max[from + 1].x - max[from].x) * slide - 3.0f;
    const float bottom = max[0].y - 1.0f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, bottom - 2.0f), ImVec2(right, bottom),
        ImGui::GetColorU32(kAccent));
    ImGui::EndDisabled();
    ImGui::SameLine();
    PositionToggle(PositionPanel::Checkout, ImGui::GetWindowPos());
}

static void CheckoutFrame(float width, float height)
{
    const ShopTheme theme;
    PreparePanel(PositionPanel::Checkout, width, height, ImVec2(width / 2, height / 2), ImVec2(0.5f, 0.5f));
    const float w = width * 0.92f, h = height * 0.92f;
    ImGui::SetNextWindowSize(ImVec2(w < 560 ? w : 560, h < 590 ? h : 590), ImGuiCond_Always);
    bool visible = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | (panelPositions[PositionIndex(PositionPanel::Checkout)].pinned ?
            ImGuiWindowFlags_NoMove : 0);
    unsigned gearCost, price[CURRENCY_COUNT];
    int pending;
    SmrCheckoutAction action = SmrCheckout(&gearCost, price, &pending);
    if (ImGui::Begin("Spend My Resources", pending ? nullptr : &visible, flags))
    {
        CheckoutTabs(pending != 0);
        ImGui::Separator();
        const bool destroyer = checkoutPanel == CheckoutPanel::Destroyer;
        const bool helldiverTab = checkoutPanel == CheckoutPanel::Helldiver, purchase = !destroyer && !helldiverTab;
        const uint64_t requisition = SmrRequisitionDue(gearCost, price);
        bool hasCharges = requisition != 0;
        for (size_t i : kUpgradeCurrencies) hasCharges = hasCharges || price[i];
        const SmrDestroyerStatus upgrade = destroyer ? SmrDestroyer(mod) : SmrDestroyerStatus{};
        const SmrHelldiverStatus helldiver = helldiverTab ? SmrHelldiver(mod) : SmrHelldiverStatus{};
        const std::string totalText = purchase ? PurchaseTotal(requisition, price) : std::string{};
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        float footer = ImGui::GetFrameHeightWithSpacing() + spacing;
        if (purchase)
            footer += ImGui::CalcTextSize(totalText.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + spacing;
        if (ImGui::BeginChild(destroyer ? "Destroyer panel" : helldiverTab ? "Helldiver panel" : "Purchase panel",
                ImVec2(0, -footer)))
        {
            if (destroyer) DestroyerPanel(upgrade, pending != 0);
            else if (helldiverTab) HelldiverPanel(helldiver, pending != 0);
            else
            {
                PurchasePanel(requisition, price);
                if (hasCharges) ImGui::TextDisabled("After purchase, you will ready automatically.");
                else if (action == SMR_CHECKOUT_CONTINUE) ImGui::TextUnformatted("Nothing to purchase.");
            }
            char message[192];
            SmrMessage(message, sizeof message);
            ImGui::TextWrapped("%s", message);
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (destroyer) DestroyerActions(pending != 0);
        else if (helldiverTab) HelldiverActions(pending != 0);
        else PurchaseActions(requisition, price, hasCharges, action, pending);
    }
    TrackPanel(PositionPanel::Checkout);
    ImGui::End();
    if (!pending && (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) CloseCheckout();
}

static constexpr float kExchangeTileSide = 48.0f;

static void ExchangeCurrencyRow(const char* role, const SmrCurrency* currencies, int count, int* selected, bool enabled)
{
    for (int i = 0; i < count; ++i)
    {
        if (i) ImGui::SameLine();
        char label[32];
        snprintf(label, sizeof label, "##exchange-%s-%d", role, i);
        if (UpgradeTile(label, kExchangeTileSide, *selected == i) && enabled) *selected = i;
        const ImVec2 min = ImGui::GetItemRectMin();
        constexpr float iconSide = 30.0f;
        const float inset = (kExchangeTileSide - iconSide) / 2;
        DrawCurrencyGraphic(static_cast<size_t>(i), ImVec2(min.x + inset, min.y + inset), iconSide);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused())
            ImGui::SetTooltip("%s (%u points)", currencies[i].name, currencies[i].points);
    }
}

static void ExchangeArrow(int count)
{
    const float width = count * kExchangeTileSide + std::max(0, count - 1) * ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, 20.0f));
    const float x = min.x + width / 2, y = min.y + 2;
    const ImU32 color = ImGui::GetColorU32(kAccent);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(x, y), ImVec2(x, y + 13), color, 2.0f);
    draw->AddLine(ImVec2(x - 5, y + 9), ImVec2(x, y + 14), color, 2.0f);
    draw->AddLine(ImVec2(x + 5, y + 9), ImVec2(x, y + 14), color, 2.0f);
}

void Frame(ImDrawList*, float width, float height, float)
{
    if (shopOpen) return;
    const ShopTheme theme;
    PreparePanel(PositionPanel::Exchange, width, height, ImVec2(width / 2, height / 2), ImVec2(0.5f, 0.5f));
    bool visible = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        (panelPositions[PositionIndex(PositionPanel::Exchange)].pinned ? ImGuiWindowFlags_NoMove : 0);
    if (ImGui::Begin("Spend My Resources - Currency Exchange", &visible, flags))
    {
        ImGui::TextUnformatted("Currency exchange (ship only)");
        ImGui::SameLine();
        PositionToggle(PositionPanel::Exchange, ImGui::GetWindowPos());
        ImGui::SameLine();
        ImGui::TextDisabled("Loadout cost:");
        ImGui::SameLine();
        PositionToggle(PositionPanel::LoadoutCost, panelPositions[PositionIndex(PositionPanel::LoadoutCost)].current);
        SmrCurrency currencies[CURRENCY_COUNT] = {};
        int count = 0;
        bool ready = true, pending = false;
        while (count < static_cast<int>(CURRENCY_COUNT) && SmrCurrencyItem(count, &currencies[count]))
        {
            if (!currencies[count].ready) ready = false;
            pending |= currencies[count].pending != 0;
            ++count;
        }
        ImGui::TextDisabled("Points:");
        for (int i = 0; i < count; ++i)
        {
            ImGui::SameLine();
            DrawCurrencyIcon(static_cast<size_t>(i), "points per unit");
            ImGui::TextDisabled("%u", currencies[i].points);
        }
        ImGui::TextUnformatted("Wallet balances:");
        for (int i = 0; i < count; ++i)
        {
            const SmrCurrency& currency = currencies[i];
            DrawCurrencyIcon(static_cast<size_t>(i), currency.cap ? "wallet balance / cap" : "wallet balance");
            if (!currency.ready) ImGui::TextUnformatted("unavailable");
            else if (currency.cap) ImGui::Text("%u / %u", currency.amount, currency.cap);
            else ImGui::Text("%u", currency.amount);
        }
        static int from = 0, to = 1, batches = 1;
        ImGui::Separator();
        ImGui::BeginDisabled(pending);
        ImGui::Text("Give: %s", from < count ? currencies[from].name : "unavailable");
        ExchangeCurrencyRow("give", currencies, count, &from, !pending);
        ExchangeArrow(count);
        ImGui::Text("Receive: %s", to < count ? currencies[to].name : "unavailable");
        ExchangeCurrencyRow("receive", currencies, count, &to, !pending);
        ImGui::InputInt("Batches", &batches);
        ImGui::EndDisabled();
        unsigned debit = 0, credit = 0, unitDebit = 0, unitCredit = 0;
        SmrCurrency source = {}, target = {};
        bool quote = batches > 0 && SmrExchangeQuote(from, to, static_cast<unsigned>(batches), &debit, &credit);
        SmrCurrencyItem(from, &source);
        SmrCurrencyItem(to, &target);
        if (SmrExchangeQuote(from, to, 1, &unitDebit, &unitCredit))
        {
            ImGui::TextUnformatted("One batch:");
            ImGui::SameLine();
            DrawCurrencyIcon(from, "given per batch");
            ImGui::Text("%u", unitDebit);
            ImGui::SameLine();
            ImGui::TextUnformatted("=");
            ImGui::SameLine();
            DrawCurrencyIcon(to, "received per batch");
            ImGui::Text("%u", unitCredit);
        }
        if (quote)
        {
            ImGui::TextUnformatted("Give");
            ImGui::SameLine();
            DrawCurrencyIcon(from, "amount to give");
            ImGui::Text("%u", debit);
            ImGui::SameLine();
            ImGui::TextUnformatted("Receive");
            ImGui::SameLine();
            DrawCurrencyIcon(to, "amount to receive");
            ImGui::Text("%u", credit);
        }
        bool funds = quote && source.amount >= debit;
        bool room = quote && static_cast<uint64_t>(target.amount) + credit <=
            (target.cap ? target.cap : UINT32_MAX);
        if (!quote) ImGui::TextDisabled("Choose different currencies and a positive, valid batch count.");
        else if (!funds) ImGui::TextDisabled("Insufficient source currency.");
        else if (!room) ImGui::TextDisabled("This exchange would exceed the destination cap.");
        if (ShopButton(pending ? "Processing..." : "Exchange", ready && !pending && funds && room, true))
            SmrExchange(mod, from, to, static_cast<unsigned>(batches));
        ImGui::TextDisabled("Exact whole-unit batches only. Unspent currency stays in your balance.");
        char message[192];
        SmrMessage(message, sizeof message);
        ImGui::PushTextWrapPos(520);
        ImGui::TextWrapped("%s", message);
        ImGui::PopTextWrapPos();
    }
    TrackPanel(PositionPanel::Exchange);
    ImGui::End();
    if (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        closeWindow();
        exchangeOpen = false;
    }
}

float PixelScale(float, float) { return 1.0f; }
bool NoticeShown() { return loadoutOpen && !shopOpen && !exchangeOpen; }
void AddModContent() {}
void ModsChanged() {}
void Overlay(ImDrawList*, float width, float height)
{
    if (!NoticeShown()) return;
    const ShopTheme theme;
    SmrLoadoutQuote quote;
    bool valid = SmrQuoteLoadout(mod, &quote) != 0;
    const ImVec2 topCenter(width / 2, 16), centerPivot(0.5f, 0);
    PreparePanel(PositionPanel::LoadoutCost, width, height, topCenter, centerPivot);
    const PanelPosition& position = panelPositions[PositionIndex(PositionPanel::LoadoutCost)];
    // The live quote can resize the card, so its default anchor follows the center.
    if (position.pinned && !position.hasPosition)
        ImGui::SetNextWindowPos(topCenter, ImGuiCond_Always, centerPivot);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
        (position.pinned ? ImGuiWindowFlags_NoInputs : 0);
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
    if (shopOpen) CheckoutFrame(width, height);
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
