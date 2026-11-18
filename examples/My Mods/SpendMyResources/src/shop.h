#pragma once
#include "hellforge_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmrItem {
    const char* name;
    unsigned price;
    int access;
} SmrItem;

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
unsigned SmrBalance(void);
int SmrBuyAccess(HfMod* mod, size_t i);

#ifdef __cplusplus
}
#endif
