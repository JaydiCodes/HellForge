#pragma once
#include "hellforge_mod.h"

typedef enum { PRICE_WEAPON, PRICE_STRATAGEM } PriceKind;

typedef struct AccessItem {
    PriceKind kind;
    uint32_t id;
    uint32_t mix_id;
    const char* name;
    unsigned requisition;
} AccessItem;

static const AccessItem kLoadoutPrices[] = {
    {PRICE_WEAPON, HF_ITEM_R36_ERUPTOR, HF_ITEM_R36_ERUPTOR, "R-36 Eruptor", 1000},
    {PRICE_STRATAGEM, HF_STRATAGEM_MS11_SOLO_SILO, 699439563u, "MS-11 Solo Silo", 1000}
};

enum { PRICE_COUNT = sizeof kLoadoutPrices / sizeof kLoadoutPrices[0] };
