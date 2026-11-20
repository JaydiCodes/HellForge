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

typedef struct SmrCurrency {
    const char* name;
    unsigned points, amount, cap;
    int ready, pending;
} SmrCurrency;

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
int SmrBalance(unsigned* out);
void SmrMessage(char* out, size_t size);
int SmrBuyCheckout(HfMod* mod);
int SmrCheckout(unsigned* total, int* pending);
int SmrTakeCheckoutRequest(void);
void SmrFinishCheckout(int ready);
int SmrDeductProfile(const char* logs, uint32_t resource, unsigned cost, char* result, size_t size);
int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size);
int SmrCurrencyItem(size_t i, SmrCurrency* out);
int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit);
int SmrExchange(HfMod* mod, size_t from, size_t to, unsigned batches);
int SmrExchangeProfile(const char* logs, uint32_t source, unsigned debit, uint32_t target, unsigned credit,
    char* result, size_t size);

#ifdef __cplusplus
}
#endif
