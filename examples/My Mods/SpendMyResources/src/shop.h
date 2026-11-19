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
} SmrItem;

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
int SmrBalance(unsigned* out);
void SmrMessage(char* out, size_t size);
int SmrBuyAccess(HfMod* mod, size_t i);

#ifdef __cplusplus
}
#endif
