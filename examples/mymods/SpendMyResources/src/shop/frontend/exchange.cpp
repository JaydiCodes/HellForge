#include "frontend_internal.h"
#include <algorithm>
#include <cstdio>

namespace editorapp
{
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
    PreparePanel(PositionPanel::Exchange, width, height);
    bool visible = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Spend My Resources - Currency Exchange", &visible, flags))
    {
        ImGui::TextUnformatted("Currency exchange (ship only)");
        ImGui::SameLine();
        ImGui::TextDisabled("F3 moves panels");
        SmrCurrency currencies[CURRENCY_COUNT] = {};
        int count = 0;
        bool ready = true, pending = false;
        while (count < static_cast<int>(CURRENCY_COUNT) && SmrCurrencyItem(count, &currencies[count]))
        {
            if (!currencies[count].ready) ready = false;
            pending |= currencies[count].pending != 0;
            ++count;
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
            ImGui::TextUnformatted("Exchange rate (one batch):");
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
}
