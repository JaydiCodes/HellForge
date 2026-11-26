#include "frontend_internal.h"
#include <algorithm>

namespace editorapp
{
CheckoutPanel checkoutPanel;
float tabIndicator;

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

static bool HasOtherCurrencyCharge(const unsigned price[CURRENCY_COUNT])
{
    for (size_t i : kUpgradeCurrencies)
        if (i != REQUISITION_SLIPS && price[i]) return true;
    return false;
}

static std::string PurchaseTotal(uint64_t requisition, const unsigned price[CURRENCY_COUNT])
{
    std::string totalDue;
    if (requisition || !HasOtherCurrencyCharge(price)) totalDue = std::to_string(requisition) + " Req";
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
    if (requisition || !HasOtherCurrencyCharge(price))
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
}

ImVec2 CheckoutSize(float width, float height)
{
    const float w = width * 0.92f, h = height * 0.92f;
    return ImVec2(w < 560 ? w : 560, h < 590 ? h : 590);
}

void CheckoutFrame(float width, float height)
{
    const ShopTheme theme;
    PreparePanel(PositionPanel::Checkout, width, height);
    ImGui::SetNextWindowSize(CheckoutSize(width, height), ImGuiCond_Always);
    bool visible = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
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
        const bool hasCharges = requisition != 0 || HasOtherCurrencyCharge(price);
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
}
