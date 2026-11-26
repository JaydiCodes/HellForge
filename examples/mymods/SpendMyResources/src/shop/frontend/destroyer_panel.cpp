#include "frontend_internal.h"
#include <cstdio>
#include <cstring>

namespace editorapp
{
size_t destroyerSelection;
uint32_t destroyerExtra;

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

const char* UpgradeName(SmrUpgrade upgrade)
{
    for (const auto& choice : kDestroyerUpgrades)
        if (choice.upgrade == upgrade) return choice.name;
    return "Destroyer Upgrade";
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

void DestroyerPanel(SmrDestroyerStatus upgrade, bool pending)
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

void DestroyerActions(bool pending)
{
    const SmrDestroyerStatus upgrade = SmrDestroyer(mod);
    if (ShopButton("Review purchase", upgrade.selected && !pending, true))
        checkoutPanel = CheckoutPanel::Purchase;
}
}
