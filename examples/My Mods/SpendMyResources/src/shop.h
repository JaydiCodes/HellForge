#pragma once
#include "hellforge_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmrItem {
    const char* name;
    unsigned price;
    int access;
    int owned;
    int pending;
    int selected;
} SmrItem;

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

#ifdef __cplusplus
}
#endif
