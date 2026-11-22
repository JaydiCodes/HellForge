#pragma once
#include <stddef.h>
#include "currencies.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmrWallet {
    unsigned amount[CURRENCY_COUNT];
} SmrWallet;

int SmrOpenLedger(const char* logs, const SmrWallet* initial, SmrWallet* saved, char* result, size_t size);
int SmrWriteLedger(const SmrWallet* wallet, char* result, size_t size);

#ifdef __cplusplus
}
#endif
