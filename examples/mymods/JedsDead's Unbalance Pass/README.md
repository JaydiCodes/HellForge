# JedsDead's UnBalance Pass

A broad balance pass on almost all weapons and stratagems

Some enemies and effects were be adjusted accordingly to not make it feel too easy or broken.

This mod also includes;
- Blunt-Force Mitigation as a free second passive in solo play.

## Knockdown mitigation settings

On your ship, press **F7** to open the mod's menu. Set **knockdown mitigation** from 0% (normal knockdown)
to 100% (a zero knockdown multiplier). The default is **70%**, the original balance-pass value.
The slider does not change impact damage or armor. Close the menu with **F7** or **Escape**.

The value saves when you release the slider, and is used again when you play solo. With other players present,
the mod removes the custom override so co-op uses the regular balance-pass stats. No Bingus Shared Loader
or Vanilla Plus Megapack is required.

Build the menu-enabled package with `.\scripts\Build-Editor.ps1 -UnbalancePass -Test` from the repository root.
With the game closed, copy `artifacts\UnbalancePass` into HellForge's `mods` folder, replacing any previous copy
of the mod, and enable it on the Mods page. The generic `Build-Mod.ps1` build does not include the overlay.