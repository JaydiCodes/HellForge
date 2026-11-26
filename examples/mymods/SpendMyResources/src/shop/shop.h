#pragma once
#include "hellforge_mod.h"
#include "../currency/currencies.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmrItem {
    const char* name;
    unsigned price;
    int access;
    int pending;
    int selected;
} SmrItem;

enum { SMR_MAX_RESTRICTED_ITEMS = 64 };
typedef struct SmrQuoteLine {
    const char* name;
    unsigned due;
} SmrQuoteLine;

typedef struct SmrLoadoutQuote {
    unsigned total, balance;
    int balance_ready;
    size_t count;
    SmrQuoteLine items[SMR_MAX_RESTRICTED_ITEMS];
} SmrLoadoutQuote;

typedef struct SmrCurrency {
    const char* name;
    unsigned points, amount, cap;
    int ready, pending;
} SmrCurrency;

typedef enum SmrUpgrade {
    SMR_UPGRADE_NONE, SMR_UPGRADE_ORBITAL, SMR_UPGRADE_EAGLE, SMR_UPGRADE_DEFENSIVE, SMR_UPGRADE_SUPPORT,
    SMR_UPGRADE_EXTRA_SLOT, SMR_UPGRADE_COUNT
} SmrUpgrade;
typedef struct SmrUpgradeTuning {
    unsigned price[CURRENCY_COUNT]; /* kCurrencies order. */
    double cooldown_percent, other_cooldown_percent;
    double cooldown_seconds, call_in_extra_seconds, rearm_seconds;
} SmrUpgradeTuning;
typedef struct SmrDestroyerStatus {
    unsigned balance[CURRENCY_COUNT];
    int available;
    SmrUpgrade selected, purchased;
    uint32_t extra;
} SmrDestroyerStatus;

enum { SMR_MAX_EXTRA_CHOICES = 149 };
typedef struct SmrExtraChoice {
    uint32_t type;
    const char* name;
    uint32_t category; /* HF_CATEGORY_... */
} SmrExtraChoice;

/* Helldiver upgrades stack: one perk from each tier. */
typedef enum SmrHelldiverTier { SMR_TIER_PLUS, SMR_TIER_ELITE, SMR_TIER_SUPER, SMR_TIER_COUNT } SmrHelldiverTier;
typedef enum SmrHelldiverPerk {
    SMR_PERK_NONE,
    SMR_PERK_PRIMARY_TRADEOFF, SMR_PERK_SECONDARY_TRADEOFF, SMR_PERK_LIGHT_KIT,
    SMR_PERK_ARMORED, SMR_PERK_SPRINTER, SMR_PERK_ENDURANCE,
    SMR_PERK_AUTO_STIM, SMR_PERK_FIELD_MEDIC,
    SMR_PERK_COUNT
} SmrHelldiverPerk;
static inline SmrHelldiverTier SmrPerkTier(SmrHelldiverPerk perk)
{
    return perk <= SMR_PERK_LIGHT_KIT ? SMR_TIER_PLUS : perk <= SMR_PERK_ENDURANCE ? SMR_TIER_ELITE : SMR_TIER_SUPER;
}
typedef struct SmrHelldiverTuning {
    unsigned price[CURRENCY_COUNT]; /* The perk's tier price, kCurrencies order. */
    double primary_ammo_percent, secondary_ammo_percent, throwables_percent;
    double armor_percent, sprint_percent, stamina_percent, movement_percent;
    double health_percent, cooldown_seconds, injury_seconds;
} SmrHelldiverTuning;
typedef struct SmrHelldiverStatus {
    unsigned balance[CURRENCY_COUNT];
    int available[SMR_TIER_COUNT];
    SmrHelldiverPerk selected[SMR_TIER_COUNT], purchased[SMR_TIER_COUNT];
} SmrHelldiverStatus;

SmrHelldiverStatus SmrHelldiver(HfMod* mod);
int SmrSelectHelldiver(HfMod* mod, SmrHelldiverTier tier, SmrHelldiverPerk perk);
const SmrHelldiverTuning* SmrGetHelldiverTuning(SmrHelldiverPerk perk);
int SmrLoadHelldiverConfig(SmrHelldiverTuning tuning[SMR_PERK_COUNT], char* result, size_t size);

SmrDestroyerStatus SmrDestroyer(HfMod* mod);
int SmrSelectUpgrade(HfMod* mod, SmrUpgrade upgrade, uint32_t extra);
size_t SmrExtraChoices(SmrExtraChoice* choices, size_t capacity);
const char* SmrExtraName(uint32_t type);
const SmrUpgradeTuning* SmrGetUpgradeTuning(SmrUpgrade upgrade);
int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size);

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
int SmrLoadoutOpen(HfMod* mod);
int SmrQuoteLoadout(HfMod* mod, SmrLoadoutQuote* out);
int SmrBalance(unsigned* out);
void SmrMessage(char* out, size_t size);
typedef enum SmrCheckoutAction {
    SMR_CHECKOUT_BLOCKED, SMR_CHECKOUT_PURCHASE, SMR_CHECKOUT_CONTINUE
} SmrCheckoutAction;
int SmrConfirmCheckout(HfMod* mod, SmrCheckoutAction action);
SmrCheckoutAction SmrCheckout(unsigned* gear_cost, unsigned upgrade_price[CURRENCY_COUNT], int* pending);
static inline uint64_t SmrRequisitionDue(unsigned gear_cost, const unsigned upgrade_price[CURRENCY_COUNT])
{
    return (uint64_t)gear_cost + upgrade_price[REQUISITION_SLIPS];
}
int SmrCheckoutComplete(void);
int SmrTakeCheckoutRequest(void);
void SmrFinishCheckout(int ready);
int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size);
int SmrCurrencyItem(size_t i, SmrCurrency* out);
int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit);
int SmrExchange(HfMod* mod, size_t from, size_t to, unsigned batches);

#ifdef __cplusplus
}
#endif
