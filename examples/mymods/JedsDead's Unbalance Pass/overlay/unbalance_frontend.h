#pragma once
#include "unbalance_pass.h"

constexpr unsigned hf_overlay_hotkey = VK_F7;
constexpr const char* hf_overlay_key_label = "F7";
inline bool CanOpenOverlay(const HfApi* api, HfMod* mod) { return api->game_state(mod) == HF_STATE_SHIP; }
inline int InitOverlayGameplay(const HfApi* api, HfMod* mod) { return UnbalanceInit(api, mod); }
