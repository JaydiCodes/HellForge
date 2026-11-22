#include <windows.h>
#include <cstring>
#include "app.h"
#include "canvas.h"
#include "icons.h"
#include "local_stats.h"
#include "sounds.h"
#include "shop_frontend.h"
#include "currency_ledger.cpp"
#include "prices.cpp"
#include "upgrade_config.cpp"

namespace editorapp
{
static const HfApi* api;
static HfMod* mod;
static void (*showCheckout)(bool);
static bool shopOpen;
static bool purchasePending;
static bool exchangeOpen;
static bool destroyerOpen;
static bool loadoutOpen;
static size_t destroyerSelection;
static uint32_t destroyerExtra;
static void (*closeWindow)();

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

static void CloseDestroyer(bool proceed)
{
    showCheckout(false);
    destroyerOpen = false;
    SmrFinishDestroyer(proceed);
}

static void CloseCheckout(bool ready = false)
{
    showCheckout(false);
    SmrFinishCheckout(ready);
    shopOpen = false;
    purchasePending = false;
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
    if (shopOpen || destroyerOpen)
    {
        closeWindow();
        return;
    }
    exchangeOpen = true;
}
void Closed() { exchangeOpen = false; }
void Tick()
{
    uintptr_t menu = 0;
    uint32_t top = 0;
    uintptr_t game = api->module_base("game.dll", nullptr);
    bool loadout = game && !api->read(game + 0x347CE38, &menu, sizeof menu) && menu &&
        !api->read(menu, &top, sizeof top) && top == 11 && api->game_state(mod) == HF_STATE_SHIP;
    loadoutOpen = loadout;
    if (!loadout)
    {
        SmrFinishCheckout(false);
        SmrTakeCheckoutRequest();
        if (destroyerOpen) CloseDestroyer(false);
        else SmrFinishDestroyer(false);
    }
    if (exchangeOpen && api->game_state(mod) != HF_STATE_SHIP)
    {
        closeWindow();
        exchangeOpen = false;
    }
    if (loadout && SmrTakeDestroyerRequest())
    {
        if (exchangeOpen)
        {
            closeWindow();
            exchangeOpen = false;
        }
        destroyerOpen = true;
        destroyerSelection = 0;
        destroyerExtra = 0;
        showCheckout(true);
    }
    if (!destroyerOpen && SmrTakeCheckoutRequest() && loadout)
    {
        if (exchangeOpen)
        {
            closeWindow();
            exchangeOpen = false;
        }
        shopOpen = true;
        purchasePending = false;
        showCheckout(true);
    }
    if (shopOpen)
    {
        if (!loadout) CloseCheckout();
        else if (purchasePending)
        {
            unsigned total, credits;
            int pending;
            SmrCheckout(&total, &credits, &pending);
            if (SmrCheckoutPaid()) CloseCheckout(true);
            else if (!pending) purchasePending = false;
        }
    }
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

static void DestroyerPicker()
{
    ImGui::TextUnformatted("Select an upgrade");
    const float side = ImGui::GetFrameHeight() * 2.75f;
    for (size_t i = 0; i < IM_ARRAYSIZE(kDestroyerUpgrades); ++i)
    {
        if (i) ImGui::SameLine();
        const bool selected = i == destroyerSelection;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        char label[48];
        snprintf(label, sizeof label, "##%s", kDestroyerUpgrades[i].name);
        if (ImGui::Button(label, ImVec2(side, side))) destroyerSelection = i;
        if (selected) ImGui::PopStyleColor();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        if (i == destroyerSelection)
            ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Text),
                ImGui::GetStyle().FrameRounding, 0, 1.5f);
        const auto& choice = kDestroyerUpgrades[i];
        const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
        if (!DrawGameStratagemIcon(choice.sprite, choice.tint, center, side * 0.76f))
            DrawUpgradeIcon(choice.icon, center, side / 52);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || ImGui::IsItemFocused())
            ImGui::SetTooltip("%s", choice.name);
    }
    ImGui::Separator();
}

static void ExtraTile(const SmrExtraChoice& choice, ImU32 tint, float side)
{
    ImGui::PushID(static_cast<int>(choice.type));
    const bool selected = choice.type == destroyerExtra;
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("##Stratagem", ImVec2(side, side))) destroyerExtra = choice.type;
    if (selected) ImGui::PopStyleColor();
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
        if (choice.type == destroyerExtra)
            draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Text), ImGui::GetStyle().FrameRounding, 0, 1.5f);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || ImGui::IsItemFocused())
        ImGui::SetTooltip("%s", choice.name);
    ImGui::PopID();
}

static bool ExtraPicker(float width, float height)
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
    const float limit = width * 0.9f;
    ImGui::TextUnformatted("Choose an extra stratagem");
    if (ImGui::BeginChild("Extra stratagem grid", ImVec2(preferred < limit ? preferred : limit, height * 0.38f),
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
                ExtraTile(choice, group.tint, side);
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

static void DestroyerFrame(float width, float height)
{
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    SmrDestroyerStatus upgrade = SmrDestroyer(mod);
    bool visible = true, proceed = false;
    if (ImGui::Begin("Spend My Resources - Destroyer Upgrades", &visible,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove))
    {
        DestroyerPicker();
        const auto& choice = kDestroyerUpgrades[destroyerSelection];
        const SmrUpgradeTuning* tuning = SmrGetUpgradeTuning(choice.upgrade);
        const bool extraSlot = choice.upgrade == SMR_UPGRADE_EXTRA_SLOT;
        ImGui::TextUnformatted(choice.name);
        ImGui::PushTextWrapPos(420);
        if (tuning) DescribeUpgrade(choice.upgrade, *tuning);
        else if (extraSlot)
            ImGui::TextWrapped("Extra slot disabled: check the extra_slot section in destroyer-upgrades.json and the mod log.");
        else ImGui::TextWrapped("Upgrades disabled: check destroyer-upgrades.json and the mod log.");
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("One solo mission. Resupply and objective stratagems are unchanged.");
        ImGui::Separator();
        if (upgrade.purchased) ImGui::Text("%s already purchased for your next solo mission.", UpgradeName(upgrade.purchased));
        if (upgrade.purchased == SMR_UPGRADE_EXTRA_SLOT)
            if (const char* name = SmrExtraName(upgrade.extra)) ImGui::Text("Extra stratagem: %s", name);
        bool extraPicked = false;
        if (tuning && !upgrade.purchased)
        {
            if (!upgrade.solo) ImGui::TextDisabled("Destroyer upgrades are available in solo play only.");
            else if (!upgrade.available) ImGui::TextDisabled("Upgrade selection unavailable.");
            else
            {
                ImGui::Text("Cost at checkout: %u Forge Credits", tuning->forge_credits);
                ImGui::TextDisabled("Available: %u Forge Credits", upgrade.credits);
                if (extraSlot) extraPicked = ExtraPicker(width, height);
            }
        }
        const bool canSelect = tuning && upgrade.available && !upgrade.purchased && (!extraSlot || extraPicked);
        ImGui::BeginDisabled(!canSelect);
        if (ImGui::Button("Select upgrade") && canSelect)
            proceed = SmrSelectUpgrade(mod, choice.upgrade, extraSlot ? destroyerExtra : 0) != 0;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(upgrade.purchased ? "Continue" : "Skip upgrade")) proceed = true;
        char message[192];
        SmrMessage(message, sizeof message);
        ImGui::PushTextWrapPos(420);
        ImGui::TextWrapped("%s", message);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (proceed) CloseDestroyer(true);
    else if (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CloseDestroyer(false);
}

static void CheckoutFrame(float width, float height)
{
    ImGui::SetNextWindowPos(ImVec2(width / 2, height / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool visible = true;
    unsigned total, credits;
    int pending;
    bool canBuy = SmrCheckout(&total, &credits, &pending) != 0;
    if (ImGui::Begin("Spend My Resources", pending ? nullptr : &visible,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        unsigned balance = 0;
        bool ready = SmrBalance(&balance) != 0;
        if (ready) ImGui::Text("Requisition slips: %u", balance);
        else ImGui::TextDisabled("Requisition slips: unavailable");
        ImGui::TextDisabled("Purchases charge your saved Spend My Resources wallet.");
        ImGui::Separator();
        SmrItem item;
        bool listed = false;
        for (size_t i = 0; SmrShopItem(i, &item); ++i)
        {
            if (!item.selected) continue;
            if (!listed) ImGui::TextUnformatted("Equipped restricted items");
            listed = true;
            if (item.access) ImGui::Text("%s - Access ready (0 Req due)", item.name);
            else ImGui::Text("%s - %u Req", item.name, item.price);
        }
        if (credits)
        {
            SmrDestroyerStatus upgrade = SmrDestroyer(mod);
            ImGui::Text("%s - %u Forge Credits", UpgradeName(upgrade.selected), credits);
            if (upgrade.selected == SMR_UPGRADE_EXTRA_SLOT)
                if (const char* name = SmrExtraName(upgrade.extra)) ImGui::Text("Extra stratagem: %s", name);
            ImGui::TextDisabled("Forge Credits available: %u", upgrade.credits);
            if (upgrade.credits < credits) ImGui::TextDisabled("Not enough Forge Credits.");
        }
        ImGui::Separator();
        if (credits && total) ImGui::Text("Total due: %u Req + %u Forge Credits", total, credits);
        else if (credits) ImGui::Text("Total due: %u Forge Credits", credits);
        else ImGui::Text("Total due: %u Req", total);
        ImGui::BeginDisabled(!canBuy);
        if (ImGui::Button(pending ? "Purchasing..." : "Purchase") && canBuy)
            purchasePending = SmrBuyCheckout(mod) != 0;
        ImGui::EndDisabled();
        ImGui::TextDisabled("After purchase, you will ready automatically.");
        char message[192];
        SmrMessage(message, sizeof message);
        ImGui::PushTextWrapPos(440);
        ImGui::TextWrapped("%s", message);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (!pending && !purchasePending && (!visible || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) CloseCheckout();
}

void Frame(ImDrawList*, float width, float height, float)
{
    if (shopOpen || destroyerOpen) return;
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
        ImGui::BeginDisabled(!ready || pending || !funds || !room);
        if (ImGui::Button(pending ? "Processing..." : "Exchange"))
            SmrExchange(mod, from, to, static_cast<unsigned>(batches));
        ImGui::EndDisabled();
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
bool NoticeShown() { return loadoutOpen && !destroyerOpen && !shopOpen && !exchangeOpen; }
void AddModContent() {}
void ModsChanged() {}
void Overlay(ImDrawList*, float width, float)
{
    if (!NoticeShown()) return;
    SmrLoadoutQuote quote;
    bool valid = SmrQuoteLoadout(&quote) != 0;
    ImGui::SetNextWindowPos(ImVec2(width - 16, 16), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (ImGui::Begin("Loadout cost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
    {
        if (!valid)
            ImGui::TextUnformatted("Loadout cost unavailable");
        else
        {
            ImGui::Text("Loadout cost: %u Req", quote.total);
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
                ImGui::TextUnformatted("Not enough requisition");
        }
    }
    ImGui::End();
}
void Modal(ImDrawList*, float width, float height, float)
{
    if (destroyerOpen) DestroyerFrame(width, height);
    else if (shopOpen) CheckoutFrame(width, height);
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
