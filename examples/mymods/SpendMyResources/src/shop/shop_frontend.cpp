#include <windows.h>
#include <algorithm>
#include <cstring>
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
static void (*closeWindow)();

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

static std::string Amount(unsigned amount, const char* name)
{
    std::string text = std::to_string(amount) + " " + name;
    if (amount == 1 && text.back() == 's') text.pop_back();
    return text;
}

static void UpgradeCostLines(const unsigned price[CURRENCY_COUNT], const unsigned balance[CURRENCY_COUNT])
{
    for (size_t i : kUpgradeCurrencies)
        if (price[i])
            ImGui::BulletText("%s (%u available%s)", Amount(price[i], kCurrencies[i].name).c_str(), balance[i],
                balance[i] < price[i] ? ", not enough" : "");
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
        ImGui::TextWrapped("Receive one owned stratagem after deployment. Your four loadout slots stay unchanged.");
        ImGui::TextWrapped("Equipped cooldowns, the extra stratagem and Eagle rearm: %+.6g%%.",
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
    std::wstring atlas;
    if (!host.modsFolder.empty())
        atlas = hfjson::Wide(host.modsFolder) + L"\\hellforge-editor\\game-icons.bin";
    if (!icons::Load(atlas) && api->log)
        api->log(mod, "No local game icon cache available; using fallback icons and named stratagem tiles.");
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
        showCheckout(true);
    }
    else if (shopOpen && SmrCheckoutComplete()) CloseCheckout(true);
}

static bool DrawGameStratagemIcon(const char* sprite, ImU32 tint, ImVec2 center, float size)
{
    if (!sprite || !icons::Texture()) return false;
    const icons::Icon* layer = icons::Find(icons::STRATAGEM, sprite);
    const icons::Icon* white = icons::Find(icons::STRATAGEM_WHITE, sprite);
    if (!layer || !white) return false;
    const auto drawLayer = [&](const icons::Icon& icon, ImU32 color) {
        const float fit = size / (icon.width > icon.height ? icon.width : icon.height);
        const float x = icon.width * fit / 2, y = icon.height * fit / 2;
        ImGui::GetWindowDrawList()->AddImage(icons::Texture(),
            ImVec2(center.x - x, center.y - y), ImVec2(center.x + x, center.y + y),
            icon.uv0, icon.uv1, ImGui::GetColorU32(color));
    };
    drawLayer(*layer, tint);
    drawLayer(*white, IM_COL32(255, 255, 238, 255));
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

static void DestroyerPicker(const SmrDestroyerStatus& upgrade, bool pending)
{
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
                if (selected != SMR_UPGRADE_EXTRA_SLOT) destroyerExtra = 0;
            }
        }
        ImGui::EndDisabled();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
        if (!DrawGameStratagemIcon(choice.sprite, choice.tint, center, side * 0.76f))
            DrawUpgradeIcon(choice.icon, center, side / 52);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || ImGui::IsItemFocused())
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
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || ImGui::IsItemFocused())
        ImGui::SetTooltip("Remove Destroyer upgrade");
    ImGui::Separator();
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || ImGui::IsItemFocused())
        ImGui::SetTooltip("%s", choice.name);
    ImGui::PopID();
}

static bool ExtraPicker(float height, bool enabled)
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
    const float remaining = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing();
    float gridHeight = height * 0.38f;
    if (gridHeight > remaining) gridHeight = remaining;
    if (ImGui::BeginChild("Extra stratagem grid", ImVec2(preferred < limit ? preferred : limit, gridHeight < 100 ? 100 : gridHeight),
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

static void DestroyerPanel(float height, SmrDestroyerStatus upgrade, bool pending)
{
    if (upgrade.purchased)
    {
        ImGui::TextWrapped("%s already purchased for your next solo mission.", UpgradeName(upgrade.purchased));
        if (upgrade.purchased == SMR_UPGRADE_EXTRA_SLOT)
            if (const char* name = SmrExtraName(upgrade.extra)) ImGui::TextWrapped("Extra stratagem: %s", name);
    }
    else if (upgrade.selected)
        ImGui::TextWrapped("Selected for purchase: %s", UpgradeName(upgrade.selected));
    else ImGui::TextDisabled("No upgrade selected");
    ImGui::Separator();
    DestroyerPicker(upgrade, pending);
    upgrade = SmrDestroyer(mod);
    const auto& choice = kDestroyerUpgrades[destroyerSelection];
    const SmrUpgradeTuning* tuning = SmrGetUpgradeTuning(choice.upgrade);
    const bool extraSlot = choice.upgrade == SMR_UPGRADE_EXTRA_SLOT;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", choice.name);
    if (tuning) DescribeUpgrade(choice.upgrade, *tuning);
    else if (extraSlot)
        ImGui::TextWrapped("Extra slot disabled: check the extra_slot section in destroyer-upgrades.json and the mod log.");
    else ImGui::TextWrapped("Upgrades disabled: check destroyer-upgrades.json and the mod log.");
    ImGui::TextWrapped("One solo mission. Resupply and objective stratagems are unchanged.");
    ImGui::Separator();
    if (tuning && !upgrade.purchased)
    {
        if (!upgrade.solo) ImGui::TextDisabled("Destroyer upgrades are available in solo play only.");
        else if (!upgrade.available) ImGui::TextDisabled("Upgrade selection unavailable.");
        else
        {
            ImGui::TextUnformatted("Cost at checkout:");
            UpgradeCostLines(tuning->price, upgrade.balance);
            if (extraSlot)
            {
                ImGui::TextWrapped("Choose an extra stratagem to select this upgrade.");
                ExtraPicker(height, !pending);
            }
        }
    }
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
        {"Armor rating (simulated via damage)", tuning.armor_percent}, {"Sprint speed", tuning.sprint_percent},
        {"Stamina", tuning.stamina_percent}, {"Movement speed", tuning.movement_percent},
    };
    for (const auto& effect : effects)
        if (effect.value) ImGui::BulletText("%s: %+.6g%%", effect.label, effect.value);
}

static void HelldiverPanel(const SmrHelldiverStatus& status, bool pending)
{
    ImGui::TextWrapped("Pick one upgrade from each tier. Tiers stack for one solo mission.");
    if (!status.solo) ImGui::TextDisabled("Helldiver upgrades are available in solo play only.");
    for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
    {
        ImGui::Separator();
        ImGui::PushID(tier);
        SmrHelldiverPerk first = SMR_PERK_NONE;
        for (int perk = SMR_PERK_NONE + 1; perk < SMR_PERK_COUNT && !first; ++perk)
            if (SmrPerkTier(static_cast<SmrHelldiverPerk>(perk)) == tier) first = static_cast<SmrHelldiverPerk>(perk);
        const SmrHelldiverTuning* price = SmrGetHelldiverTuning(first);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", kHelldiverTiers[tier]);
        if (status.purchased[tier])
            ImGui::TextWrapped("%s already purchased for your next solo mission.", PerkName(status.purchased[tier]));
        else if (!price)
            ImGui::TextWrapped("Helldiver upgrades disabled: check helldiver-upgrades.json and the mod log.");
        else if (status.available[tier]) UpgradeCostLines(price->price, status.balance);
        for (int perk = SMR_PERK_NONE + 1; perk < SMR_PERK_COUNT; ++perk)
        {
            const auto choice = static_cast<SmrHelldiverPerk>(perk);
            const SmrHelldiverTuning* tuning = SmrGetHelldiverTuning(choice);
            if (SmrPerkTier(choice) != tier || !tuning) continue;
            const bool selected = status.selected[tier] == choice || status.purchased[tier] == choice;
            const bool enabled = status.available[tier] && !pending;
            ImGui::PushID(perk);
            ImGui::BeginDisabled(!enabled);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Header, kSelectedSurface);
            if (ImGui::Selectable(kPerkNames[perk], selected) && enabled)
                SmrSelectHelldiver(mod, static_cast<SmrHelldiverTier>(tier),
                    status.selected[tier] == choice ? SMR_PERK_NONE : choice);
            if (selected) ImGui::PopStyleColor();
            HoverHighlight(kPerkNames[perk], enabled);
            ImGui::EndDisabled();
            ImGui::Indent();
            DescribePerk(choice, *tuning);
            ImGui::Unindent();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

static void HelldiverActions(const SmrHelldiverStatus& status, bool pending)
{
    bool any = false;
    for (SmrHelldiverPerk perk : status.selected) any = any || perk != SMR_PERK_NONE;
    if (ShopButton("Review purchase", any && !pending, true)) checkoutPanel = CheckoutPanel::Purchase;
    if (any)
    {
        ImGui::SameLine();
        if (ShopButton("Remove upgrades", !pending))
            for (int tier = 0; tier < SMR_TIER_COUNT; ++tier)
                if (status.selected[tier]) SmrSelectHelldiver(mod, static_cast<SmrHelldiverTier>(tier), SMR_PERK_NONE);
    }
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
        ImGui::Text("Requisition slips: %u", balance);
        if (balance < requisition)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarning), "Not enough requisition for this purchase.");
    }
    else ImGui::TextDisabled("Requisition slips: unavailable");
    ImGui::TextDisabled("Purchases charge your saved Spend My Resources wallet.");
    ImGui::Separator();
    SmrItem item;
    bool listed = false;
    for (size_t i = 0; SmrShopItem(i, &item); ++i)
    {
        if (!item.selected) continue;
        if (!listed) ImGui::TextUnformatted("Restricted item access");
        listed = true;
        if (item.access) ImGui::TextWrapped("%s - Access ready (0 Req due)", item.name);
        else ImGui::TextWrapped("%s - %u Req", item.name, item.price);
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
        UpgradeCostLines(price, upgrade.balance);
        if (!upgrade.solo) ImGui::TextDisabled("Remove the solo-only upgrades from the Destroyer and Helldiver tabs.");
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

static void PurchaseActions(const std::string& totalText, bool hasCharges, SmrCheckoutAction action, int& pending)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentText);
    ImGui::TextWrapped("%s", totalText.c_str());
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

static void CheckoutFrame(float width, float height)
{
    const ShopTheme theme;
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const float w = width * 0.92f, h = height * 0.92f;
    ImGui::SetNextWindowSize(ImVec2(w < 560 ? w : 560, h < 590 ? h : 590), ImGuiCond_Always);
    bool visible = true;
    unsigned gearCost, price[CURRENCY_COUNT];
    int pending;
    SmrCheckoutAction action = SmrCheckout(&gearCost, price, &pending);
    if (ImGui::Begin("Spend My Resources", pending ? nullptr : &visible,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
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
            if (destroyer) DestroyerPanel(height, upgrade, pending != 0);
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
        else if (helldiverTab) HelldiverActions(helldiver, pending != 0);
        else PurchaseActions(totalText, hasCharges, action, pending);
    }
    ImGui::End();
    if (!pending && (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) CloseCheckout();
}

void Frame(ImDrawList*, float width, float height, float)
{
    if (shopOpen) return;
    const ShopTheme theme;
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool visible = true;
    if (ImGui::Begin("Spend My Resources - Currency Exchange", &visible,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("Currency exchange (ship only)");
        ImGui::TextDisabled("Req 1 | Common 50 | Rare 100 | Super 200 | Forge Credits 750 points");
        SmrCurrency currency;
        int count = 0;
        bool ready = true, pending = false;
        while (SmrCurrencyItem(count, &currency))
        {
            if (!currency.ready) ready = false;
            pending |= currency.pending != 0;
            if (!currency.ready) ImGui::Text("%s: unavailable", currency.name);
            else if (currency.cap) ImGui::Text("%s: %u / %u", currency.name, currency.amount, currency.cap);
            else ImGui::Text("%s: %u", currency.name, currency.amount);
            ++count;
        }
        static int from = 0, to = 1, batches = 1;
        const auto choose = [](const char* label, int* selected)
        {
            SmrCurrency choice;
            if (!SmrCurrencyItem(*selected, &choice)) return;
            if (ImGui::BeginCombo(label, choice.name))
            {
                for (int i = 0; SmrCurrencyItem(i, &choice); ++i)
                    if (ImGui::Selectable(choice.name, i == *selected)) *selected = i;
                ImGui::EndCombo();
            }
        };
        ImGui::Separator();
        ImGui::BeginDisabled(pending);
        choose("Give", &from);
        choose("Receive", &to);
        ImGui::InputInt("Batches", &batches);
        ImGui::EndDisabled();
        unsigned debit = 0, credit = 0, unitDebit = 0, unitCredit = 0;
        SmrCurrency source = {}, target = {};
        bool quote = batches > 0 && SmrExchangeQuote(from, to, static_cast<unsigned>(batches), &debit, &credit);
        SmrCurrencyItem(from, &source);
        SmrCurrencyItem(to, &target);
        if (SmrExchangeQuote(from, to, 1, &unitDebit, &unitCredit))
            ImGui::Text("One batch: %u %s = %u %s", unitDebit, source.name, unitCredit, target.name);
        if (quote) ImGui::Text("Give %u %s; receive %u %s", debit, source.name, credit, target.name);
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
void Overlay(ImDrawList*, float width, float)
{
    if (!NoticeShown()) return;
    const ShopTheme theme;
    SmrLoadoutQuote quote;
    bool valid = SmrQuoteLoadout(mod, &quote) != 0;
    ImGui::SetNextWindowPos(ImVec2(width - 16, 16), ImGuiCond_Always, ImVec2(1, 0));
    if (ImGui::Begin("Loadout cost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
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
    ImGui::End();
}
void Modal(ImDrawList*, float width, float height, float)
{
    if (shopOpen) CheckoutFrame(width, height);
}
void SaveStatus(bool, const std::string&) {}
}

// The shop does not use the editor's fonts, sounds or stats saving.
namespace ui { bool BuildFonts(float) { return false; } }
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
