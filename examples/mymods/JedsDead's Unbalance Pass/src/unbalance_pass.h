#pragma once
#include "hellforge_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { UNBALANCE_DEFAULT_MITIGATION = 60 };
typedef enum UnbalanceMitigationState {
    UNBALANCE_WAITING, UNBALANCE_COOP, UNBALANCE_SOLO, UNBALANCE_REFUSED, UNBALANCE_RESTORE_REFUSED
} UnbalanceMitigationState;

int UnbalanceInit(const HfApi* api, HfMod* mod);
void UnbalanceRequestMitigation(int percent);
UnbalanceMitigationState UnbalanceMitigationStatus(void);
void UnbalanceSetProjectileSwaps(int enabled);

#ifdef __cplusplus
}
#endif
