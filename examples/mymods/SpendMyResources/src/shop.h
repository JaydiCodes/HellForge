#pragma once
#include "hellforge_mod.h"

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
    SMR_UPGRADE_COUNT
} SmrUpgrade;
typedef struct SmrUpgradeTuning {
    unsigned forge_credits;
    double cooldown_percent, other_cooldown_percent;
    double cooldown_seconds, call_in_extra_seconds, rearm_seconds;
    unsigned uses;
} SmrUpgradeTuning;
typedef struct SmrDestroyerStatus {
    unsigned credits;
    int available, solo;
    SmrUpgrade selected, purchased;
} SmrDestroyerStatus;

int SmrTakeDestroyerRequest(void);
void SmrFinishDestroyer(int proceed);
SmrDestroyerStatus SmrDestroyer(HfMod* mod);
int SmrSelectUpgrade(HfMod* mod, SmrUpgrade upgrade);
const SmrUpgradeTuning* SmrGetUpgradeTuning(SmrUpgrade upgrade);
int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size);

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
int SmrQuoteLoadout(SmrLoadoutQuote* out);
int SmrBalance(unsigned* out);
void SmrMessage(char* out, size_t size);
int SmrBuyCheckout(HfMod* mod);
int SmrCheckout(unsigned* total, unsigned* credits, int* pending);
int SmrCheckoutPaid(void);
int SmrTakeCheckoutRequest(void);
void SmrFinishCheckout(int ready);
int SmrDeductProfile(const char* logs, uint32_t resource, unsigned cost, char* result, size_t size);
int SmrDeductProfilePair(const char* logs, uint32_t resource, unsigned cost, uint32_t second, unsigned second_cost,
    char* result, size_t size);
int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size);
int SmrCurrencyItem(size_t i, SmrCurrency* out);
int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit);
int SmrExchange(HfMod* mod, size_t from, size_t to, unsigned batches);
int SmrExchangeProfile(const char* logs, uint32_t source, unsigned debit, uint32_t target, unsigned credit,
    char* result, size_t size);

#ifdef __cplusplus
}
#endif
