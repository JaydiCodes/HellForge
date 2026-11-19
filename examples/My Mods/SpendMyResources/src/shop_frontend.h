#pragma once
#include "shop.h"

constexpr unsigned hf_overlay_hotkey = 0;
constexpr const char* hf_overlay_key_label = "Ready";
inline bool CanOpenOverlay(const HfApi*, HfMod*) { return false; }
inline int InitOverlayGameplay(const HfApi* api, HfMod* mod) { return SmrInit(api, mod); }
