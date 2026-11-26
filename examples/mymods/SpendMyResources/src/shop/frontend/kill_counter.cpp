#include "frontend_internal.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace editorapp
{
/* The kill reward card: fades in, counts to the new amount, and fades out once no kill has come for 5 s. */
static constexpr ULONGLONG kKillFadeInMs = 250, kKillCountMs = 700, kKillHoldMs = 5000, kKillFadeOutMs = 500;
static constexpr ULONGLONG kSummaryDelayMs = 3000, kSummaryHoldMs = 10000;
ULONGLONG (WINAPI* killClock)() = GetTickCount64;
struct KillCard
{
    uint32_t sequence;
    ULONGLONG shownAt, killAt;
    double from;
    uint64_t reward;
    bool visible, summary;
};
static KillCard killCard;
static SmrKillSummary missionSummary;
static bool summaryPending, wasOnShip;
static ULONGLONG shipAt;

bool KillCardShowing(ULONGLONG now)
{
    const uint32_t state = api->game_state(mod);
    const ULONGLONG hold = killCard.summary ? kSummaryHoldMs : kKillHoldMs;
    const bool inPlace = state == (killCard.summary ? HF_STATE_SHIP : HF_STATE_MISSION);
    if (killCard.visible && (!inPlace || now - killCard.killAt >= hold + kKillFadeOutMs))
        killCard.visible = false;
    return killCard.visible;
}

bool KillSummaryShowing(ULONGLONG now)
{
    return killCard.summary && KillCardShowing(now);
}

float KillCardAlpha(ULONGLONG now)
{
    if (!KillCardShowing(now)) return 0.0f;
    const ULONGLONG quiet = now - killCard.killAt;
    const ULONGLONG hold = killCard.summary ? kSummaryHoldMs : kKillHoldMs;
    const float in = std::min(1.0f, static_cast<float>(now - killCard.shownAt) / kKillFadeInMs);
    const float out = quiet <= hold ? 1.0f :
        1.0f - static_cast<float>(quiet - hold) / kKillFadeOutMs;
    return in * out;
}

double KillCardAmount(ULONGLONG now)
{
    const double t = std::min(1.0, static_cast<double>(now - killCard.killAt) / kKillCountMs);
    const double eased = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    return killCard.from + (killCard.reward - killCard.from) * eased;
}

// A kill while the card shows counts on from the amount on screen, and fades back in from the current alpha.
void FollowKills()
{
    SmrKillNotice notice;
    SmrLastKill(&notice);
    if (notice.sequence == killCard.sequence) return;
    if (api->game_state(mod) != HF_STATE_MISSION)
    {
        killCard.sequence = notice.sequence;
        return;
    }
    const ULONGLONG now = killClock();
    const bool showing = KillCardShowing(now);
    killCard.from = showing ? KillCardAmount(now) : notice.before - notice.base;
    killCard.shownAt = now - static_cast<ULONGLONG>(KillCardAlpha(now) * kKillFadeInMs);
    killCard.sequence = notice.sequence;
    killCard.reward = notice.after - notice.base;
    killCard.killAt = now;
    killCard.visible = true;
    killCard.summary = false;
}

void FollowKillSummary()
{
    const ULONGLONG now = killClock();
    const bool onShip = api->game_state(mod) == HF_STATE_SHIP;
    if (wasOnShip && !onShip)
    {
        summaryPending = false;
        if (killCard.summary) killCard.visible = false;
    }
    if (onShip && !wasOnShip) shipAt = now;
    wasOnShip = onShip;

    SmrKillSummary latest;
    SmrLastKillSummary(&latest);
    if (latest.sequence != missionSummary.sequence)
    {
        missionSummary = latest;
        summaryPending = latest.sequence != 0;
    }
    if (!onShip || !summaryPending || now - shipAt < kSummaryDelayMs) return;
    summaryPending = false;
    killCard.from = static_cast<double>(missionSummary.share + missionSummary.bonus);
    killCard.reward = missionSummary.share + missionSummary.bonus;
    killCard.shownAt = killCard.killAt = now;
    killCard.visible = killCard.summary = true;
}

static void CenterNextLine(float width)
{
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) / 2);
}

void KillRewardFrame(float width, float height)
{
    const ULONGLONG now = killClock();
    if (!KillCardShowing(now)) return;
    const float alpha = KillCardAlpha(now);
    ImVec2 place, pivot;
    PanelPlace(PositionPanel::KillReward, width, height, &place, &pivot);
    ImGui::SetNextWindowPos(place, ImGuiCond_Always, pivot);
    // A minimum width keeps the centered card from jittering as the digits change.
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("Kill reward", nullptr, flags))
    {
        const char* title = killCard.summary ? "Mission kill rewards" : "Kill reward";
        CenterNextLine(ImGui::CalcTextSize(title).x);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentText), "%s", title);
        const uint64_t reward = static_cast<uint64_t>(std::llround(KillCardAmount(now)));
        const std::string amount = std::to_string(reward);
        CenterNextLine(kCurrencyIconSize + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(amount.c_str()).x);
        DrawCurrencyAmount(REQUISITION_SLIPS, reward);
        if (killCard.summary)
        {
            ImGui::Separator();
            ImGui::Text("Team kills: %u (%llu Req)", missionSummary.team_kills,
                static_cast<unsigned long long>(missionSummary.team));
            ImGui::Text("Your kills: %u (%llu Req)", missionSummary.own_kills,
                static_cast<unsigned long long>(missionSummary.mine));
            ImGui::Text("Your share (%u player%s): %llu Req", missionSummary.players,
                missionSummary.players == 1 ? "" : "s",
                static_cast<unsigned long long>(missionSummary.share));
            ImGui::Text("Contribution bonus: %llu Req", static_cast<unsigned long long>(missionSummary.bonus));
            ImGui::TextDisabled("Earned reward before the wallet cap.");
        }
    }
    TrackPanel(PositionPanel::KillReward);
    ImGui::End();
    ImGui::PopStyleVar();
}
}
