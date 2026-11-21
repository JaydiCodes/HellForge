#pragma once
#include <stdint.h>

static const struct {
    uint32_t id;
    const char* name;
    unsigned points;
} kCurrencies[] = {
    {0xD7150EECu, "Requisition slips", 1},
    {0xEDF6EAF5u, "Common samples", 50},
    {0xB1ED1C41u, "Rare samples", 100},
    {0xDAC0E9DBu, "Super samples", 200},
    {0x6C3C83C1u, "Forge Credits", 750}
};
enum { FORGE_CREDITS = 4, CURRENCY_COUNT = sizeof kCurrencies / sizeof kCurrencies[0] };
