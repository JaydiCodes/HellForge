#include "frontend_internal.h"
#include <algorithm>

namespace editorapp
{
SmrHelldiverPerk helldiverSelection = SMR_PERK_PRIMARY_TRADEOFF;
const char* const kHelldiverTiers[SMR_TIER_COUNT] = {"Plus", "Elite", "Super"};
static constexpr const char* kPerkNames[SMR_PERK_COUNT] = {
    "None", "Primary Ammo Trade-off", "Secondary Ammo Trade-off", "Grenade Kit", "Armored", "Sprinter", "Endurance",
    "Auto-Stim", "Field Medic",
};
const PerkIcon kPerkIcons[SMR_PERK_COUNT] = {
    {icons::WEAPON, nullptr, ""},
    {icons::WEAPON, "P-2 Peacemaker", "PA"},
    {icons::WEAPON, "AR-23 Liberator", "SA"},
    {icons::PASSIVE, nullptr, "GK"},
    {icons::PASSIVE, "EXTRA PADDING", "AR"},
    {icons::BOOSTER, "Dead Sprint", "SP"},
    {icons::BOOSTER, "Stamina Enhancement", "EN"},
    {icons::BOOSTER, "Experimental Infusion", "AS"},
    {icons::PASSIVE, "MED-KIT", "FM"},
};

const char* PerkName(SmrHelldiverPerk perk)
{
    return perk > SMR_PERK_NONE && perk < SMR_PERK_COUNT ? kPerkNames[perk] : "Helldiver Upgrade";
}

static void DescribePerk(SmrHelldiverPerk perk, const SmrHelldiverTuning& tuning)
{
    if (perk == SMR_PERK_AUTO_STIM)
        ImGui::BulletText("Auto-heal at %.6g%% health or lower, once every %.6g s.", tuning.health_percent,
            tuning.cooldown_seconds);
    if (perk == SMR_PERK_FIELD_MEDIC) ImGui::BulletText("Regenerate 1 HP every second.");
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

void HelldiverPanel(const SmrHelldiverStatus& status, bool pending)
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

void HelldiverActions(bool pending)
{
    const SmrHelldiverStatus status = SmrHelldiver(mod);
    bool any = false;
    for (SmrHelldiverPerk perk : status.selected) any = any || perk != SMR_PERK_NONE;
    if (ShopButton("Review purchase", any && !pending, true)) checkoutPanel = CheckoutPanel::Purchase;
}
}
