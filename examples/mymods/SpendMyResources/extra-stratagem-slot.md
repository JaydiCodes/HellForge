# Extra Stratagem Slot

In solo play, press **Ready**, choose the plus icon in the **Destroyer** panel, and pick an owned stratagem
not already in your loadout from the icon grid. The grid uses the local HellForge Editor game icon cache when
available and shows named tiles otherwise. **Select upgrade** shows its costs in **Purchase**. You can switch back
to **Destroyer** to change or remove it. Only clicking the **Purchase** button charges you.

The four native loadout slots stay unchanged. The extra appears in the mission's stratagem menu after deployment,
with its normal uses. It isn't saved to the game's loadout. Restricted extras also need requisition access, just like
restricted items in the native slots.

The yellow underline marks the active panel, and yellow borders mark the previewed upgrade and chosen extra.
Hover highlights fade in and out without moving the controls.

The stratagem grid uses the available panel height and scrolls when needed. Destroyer costs use a compact summary.
Use **Unpin** to move the checkout or currency exchange panel. The F2 exchange panel also controls the loadout cost
panel. **Pin** saves each panel's position for the next session.

Upgrade costs show a currency icon and the wallet balance before the price, such as `15 / 200`. Hover an icon to
see its currency name. A balance below the price is highlighted.

The standalone build converts the local SVGs in `src\images` to PNGs in the mod's `images` folder. Forge Credits
uses HellForge's icon. Keep game artwork local. Missing or unreadable images use fallback icons and are logged.

The upgrade costs 2,000 requisition slips, 50 Forge Credits, 10 Common samples, 5 Rare samples and 1 Super sample
by default and increases equipped cooldowns, including the extra and Eagle rearm, by 10%. Edit `forge_credits`,
`common_samples`, `rare_samples`, `super_samples` and `other_cooldown_percent` in `extra_slot` in
[destroyer-upgrades.json](./destroyer-upgrades.json) beside the mod DLL, then restart the game. A config
without `extra_slot` still enables the other four upgrades.

All five upgrades default to 2,000 Req each. Set `requisition` in each upgrade's section to a nonnegative whole
number (use `0` for no Req charge). Older configs without that setting use 2,000 Req. The Purchase total adds
the upgrade's Req price to any unpaid gear access, including access for a restricted extra.

An extra Eagle shares **Eagle Rearm** with any Eagles already equipped. If the mission doesn't already have Rearm,
the extra Eagle needs room for both entries. The extra's packages must also be available. If deployment cannot be
verified, the paid upgrade is retained for a later eligible solo mission in the same game session.
