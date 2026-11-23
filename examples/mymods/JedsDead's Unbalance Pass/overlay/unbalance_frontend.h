#pragma once
#include "unbalance_pass.h"

constexpr unsigned hf_overlay_hotkey = VK_F7;
constexpr const char* hf_overlay_key_label = "F7";
inline bool CanOpenOverlay(const HfApi* api, HfMod* mod)
{
    const uint32_t state = api->game_state(mod);
    return state == HF_STATE_SHIP || state == HF_STATE_MISSION;
}
inline int InitOverlayGameplay(const HfApi* api, HfMod* mod) { return UnbalanceInit(api, mod); }
