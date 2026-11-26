#include "frontend_internal.h"
#include <algorithm>

namespace editorapp
{
static constexpr struct {
    ImGuiCol slot;
    ImU32 color;
} kShopPalette[] = {
    {ImGuiCol_Text, kText},
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

ShopTheme::ShopTheme()
{
    for (const auto& entry : kShopPalette) ImGui::PushStyleColor(entry.slot, entry.color);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
}

ShopTheme::~ShopTheme()
{
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(IM_ARRAYSIZE(kShopPalette));
}

static float AnimateHighlight(float progress, bool on, float seconds)
{
    const float step = ImGui::GetIO().DeltaTime / seconds;
    return std::clamp(progress + (on ? step : -step), 0.0f, 1.0f);
}

void HoverHighlight(const char* label, bool enabled)
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

bool ShopButton(const char* label, bool enabled, bool primary)
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

bool UpgradeTile(const char* label, float side, bool selected)
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

const char* const kCurrencyIcons[CURRENCY_COUNT] = {
    "Requisition_Slip.png", "Common_Sample_Icon.png", "Rare_Sample_Icon.png",
    "Super_Sample_Icon.png", "Forge_Credit.png",
};

std::string Amount(unsigned amount, const char* name)
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

void DrawCurrencyGraphic(size_t currency, ImVec2 min, float side)
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

void DrawCurrencyIcon(size_t currency, const char* detail)
{
    DrawCurrencyGraphic(currency, ImGui::GetCursorScreenPos(), kCurrencyIconSize);
    ImGui::Dummy(ImVec2(kCurrencyIconSize, kCurrencyIconSize));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s: %s", kCurrencies[currency].name, detail);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

void DrawCurrencyAmount(size_t currency, uint64_t amount)
{
    DrawCurrencyIcon(currency);
    ImGui::Text("%llu", static_cast<unsigned long long>(amount));
}

void UpgradeCostValues(const unsigned price[CURRENCY_COUNT], const unsigned balance[CURRENCY_COUNT])
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

void DrawIconLayer(const icons::Icon& icon, ImU32 color, ImVec2 center, float size)
{
    const float fit = size / (icon.width > icon.height ? icon.width : icon.height);
    const float x = icon.width * fit / 2, y = icon.height * fit / 2;
    ImGui::GetWindowDrawList()->AddImage(icons::Texture(),
        ImVec2(center.x - x, center.y - y), ImVec2(center.x + x, center.y + y),
        icon.uv0, icon.uv1, ImGui::GetColorU32(color));
}
}
