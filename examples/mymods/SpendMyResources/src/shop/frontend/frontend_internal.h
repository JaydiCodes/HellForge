#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include "app.h"
#include "icons.h"
#include "../shop_frontend.h"

namespace editorapp
{
extern const HfApi* api;
extern HfMod* mod;
extern void (*closeWindow)();
extern bool shopOpen, exchangeOpen, layoutOpen;

void SetScreens(bool shop, bool layout);
void CloseCheckout(bool ready = false);

enum class PositionPanel { Checkout, Exchange, LoadoutCost, KillReward, Count };
constexpr size_t kPanelCount = static_cast<size_t>(PositionPanel::Count);
struct PanelPosition
{
    ImVec2 position;
    ImVec2 size;
    bool hasPosition;
    bool initialized;
};
extern PanelPosition panelPositions[kPanelCount];
extern std::wstring panelPositionsPath;
extern bool panelPositionsLoaded;
extern bool (*layoutKeyDown)();

inline size_t PositionIndex(PositionPanel panel) { return static_cast<size_t>(panel); }
void InitPanelPositions();
void LoadPanelPositions();
void SavePanelPositions();
void PanelPlace(PositionPanel panel, float width, float height, ImVec2* position, ImVec2* pivot);
void PreparePanel(PositionPanel panel, float width, float height);
void TrackPanel(PositionPanel panel);
void OpenLayout();
void CloseLayout();
void FollowLayoutKey();
void LayoutFrame(float width, float height);

constexpr ImU32 kText = IM_COL32(238, 240, 238, 255);
constexpr ImU32 kAccent = IM_COL32(250, 219, 67, 255);
constexpr ImU32 kAccentText = IM_COL32(255, 235, 147, 255);
constexpr ImU32 kWarning = IM_COL32(251, 161, 119, 255);
constexpr ImU32 kSurface = IM_COL32(32, 38, 46, 255);
constexpr ImU32 kSelectedSurface = IM_COL32(63, 57, 34, 255);
constexpr ImU32 kBorder = IM_COL32(82, 93, 104, 255);
constexpr float kCurrencyIconSize = 16.0f;
inline constexpr size_t kUpgradeCurrencies[] = {
    REQUISITION_SLIPS, FORGE_CREDITS, COMMON_SAMPLES, RARE_SAMPLES, SUPER_SAMPLES
};
extern const char* const kCurrencyIcons[CURRENCY_COUNT];

struct ShopTheme
{
    ShopTheme();
    ~ShopTheme();
};
void HoverHighlight(const char* label, bool enabled = true);
bool ShopButton(const char* label, bool enabled, bool primary = false);
bool UpgradeTile(const char* label, float side, bool selected);
std::string Amount(unsigned amount, const char* name);
void DrawIconLayer(const icons::Icon& icon, ImU32 color, ImVec2 center, float size);
void DrawCurrencyGraphic(size_t currency, ImVec2 min, float side);
void DrawCurrencyIcon(size_t currency, const char* detail = "wallet balance / cost");
void DrawCurrencyAmount(size_t currency, uint64_t amount);
void UpgradeCostValues(const unsigned price[CURRENCY_COUNT], const unsigned balance[CURRENCY_COUNT]);

enum class CheckoutPanel { Destroyer, Helldiver, Purchase };
extern CheckoutPanel checkoutPanel;
extern float tabIndicator;
extern size_t destroyerSelection;
extern uint32_t destroyerExtra;
extern SmrHelldiverPerk helldiverSelection;
extern const char* const kHelldiverTiers[SMR_TIER_COUNT];
struct PerkIcon
{
    icons::Kind kind;
    const char* sprite;
    const char* abbreviation;
};
extern const PerkIcon kPerkIcons[SMR_PERK_COUNT];

const char* UpgradeName(SmrUpgrade upgrade);
void DestroyerPanel(SmrDestroyerStatus upgrade, bool pending);
void DestroyerActions(bool pending);
const char* PerkName(SmrHelldiverPerk perk);
void HelldiverPanel(const SmrHelldiverStatus& status, bool pending);
void HelldiverActions(bool pending);
ImVec2 CheckoutSize(float width, float height);
void CheckoutFrame(float width, float height);

extern ULONGLONG (WINAPI* killClock)();
bool KillCardShowing(ULONGLONG now);
bool KillSummaryShowing(ULONGLONG now);
float KillCardAlpha(ULONGLONG now);
double KillCardAmount(ULONGLONG now);
void FollowKills();
void FollowKillSummary();
void KillRewardFrame(float width, float height);
}
