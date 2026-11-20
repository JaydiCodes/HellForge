#pragma once
#include "shop.h"

constexpr unsigned hf_overlay_hotkey = VK_F2;
constexpr const char* hf_overlay_key_label = "F2";
inline bool CanOpenOverlay(const HfApi* api, HfMod* mod) { return api->game_state(mod) == HF_STATE_SHIP; }
inline int InitOverlayGameplay(const HfApi* api, HfMod* mod) { return SmrInit(api, mod); }
