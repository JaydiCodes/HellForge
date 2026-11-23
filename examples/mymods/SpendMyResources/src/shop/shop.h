#pragma once
#include "hellforge_mod.h"
#include "currencies.h"

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
    unsigned price[CURRENCY_COUNT]; /* kCurrencies order; upgrades never cost requisition. */
    double cooldown_percent, other_cooldown_percent;
    double cooldown_seconds, call_in_extra_seconds, rearm_seconds;
} SmrUpgradeTuning;
typedef struct SmrDestroyerStatus {
    unsigned balance[CURRENCY_COUNT];
    int available, solo;
    SmrUpgrade selected, purchased;
    uint32_t extra;
} SmrDestroyerStatus;

enum { SMR_MAX_EXTRA_CHOICES = 149 };
typedef struct SmrExtraChoice {
    uint32_t type;
    const char* name;
    uint32_t category; /* HF_CATEGORY_... */
} SmrExtraChoice;

int SmrTakeDestroyerRequest(void);
void SmrFinishDestroyer(int proceed);
SmrDestroyerStatus SmrDestroyer(HfMod* mod);
int SmrSelectUpgrade(HfMod* mod, SmrUpgrade upgrade, uint32_t extra);
size_t SmrExtraChoices(SmrExtraChoice* choices, size_t capacity);
const char* SmrExtraName(uint32_t type);
const SmrUpgradeTuning* SmrGetUpgradeTuning(SmrUpgrade upgrade);
int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size);

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
int SmrQuoteLoadout(SmrLoadoutQuote* out);
int SmrBalance(unsigned* out);
void SmrMessage(char* out, size_t size);
int SmrBuyCheckout(HfMod* mod);
int SmrCheckout(unsigned* total, unsigned upgrade_price[CURRENCY_COUNT], int* pending);
int SmrCheckoutPaid(void);
int SmrTakeCheckoutRequest(void);
void SmrFinishCheckout(int ready);
int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size);
int SmrCurrencyItem(size_t i, SmrCurrency* out);
int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit);
int SmrExchange(HfMod* mod, size_t from, size_t to, unsigned batches);

#ifdef __cplusplus
}
#endif
