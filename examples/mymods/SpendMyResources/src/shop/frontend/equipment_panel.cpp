#include "frontend_internal.h"
#include <algorithm>

namespace editorapp
{
void EquipmentPanel(bool pending)
{
    const SmrEquipmentStatus status = SmrEquipment();
    if (!status.configured)
    {
        ImGui::TextWrapped("Equipment upgrades disabled: check equipment-upgrades.json and the mod log.");
        return;
    }
    bool any = false;
    for (const auto& row : status.tiers)
    {
        if (!row.group) continue;
        const auto& group = *row.group;
        if (!any)
        {
            ImGui::TextUnformatted(group.weapon);
            ImGui::TextWrapped("Pick one upgrade per tier for one mission.");
        }
        any = true;
        ImGui::PushID(group.tier);
        ImGui::SeparatorText(kHelldiverTiers[group.tier]);
        unsigned selected = row.selected;
        unsigned preview = row.purchased ? row.purchased : selected ? selected : 1;
        unsigned hovered = 0;
        const bool enabled = row.available && !pending;
        for (unsigned i = 1; i <= group.count; ++i)
        {
            const char* title = group.choices[i - 1].title;
            const float side = std::max(ImGui::GetFrameHeight() * 2.5f,
                ImGui::CalcTextSize(title).x + 2 * ImGui::GetStyle().FramePadding.x);
            if (i > 1 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + side <
                    ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x)
                ImGui::SameLine();
            ImGui::BeginDisabled(!enabled);
            if (UpgradeTile(title, side, row.purchased == i || selected == i) && enabled)
            {
                const unsigned pick = selected == i ? 0 : i;
                if (SmrSelectEquipment(mod, &group, pick)) selected = pick;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemFocused()) preview = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) hovered = i;
        }
        const auto& choice = group.choices[(hovered ? hovered : preview) - 1];
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", choice.title);
        for (size_t i = 0; i < choice.edit_count; ++i)
            ImGui::BulletText("%s: %.6g", choice.edits[i].label, choice.edits[i].value);
        if (row.purchased) ImGui::TextWrapped("Already purchased: %s", group.choices[row.purchased - 1].title);
        else
        {
            if (selected) ImGui::TextWrapped("Selected for purchase: %s", group.choices[selected - 1].title);
            else ImGui::TextDisabled("No %s upgrade selected", kHelldiverTiers[group.tier]);
            ImGui::TextUnformatted("Cost:");
            ImGui::SameLine();
            UpgradeCostValues(row.price, status.balance);
        }
        ImGui::PopID();
    }
    if (!any) ImGui::TextWrapped("No upgrades available for your equipped primary.");
}

void EquipmentActions(bool pending)
{
    bool selected = false;
    for (const auto& row : SmrEquipment().tiers) selected |= row.selected != 0;
    if (ShopButton("Review purchase", selected && !pending, true)) checkoutPanel = CheckoutPanel::Purchase;
}
}
