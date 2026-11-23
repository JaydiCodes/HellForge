# Extra Stratagem Slot

In solo play, press **Ready**, choose the plus icon in Destroyer Upgrades, and pick an owned stratagem
not already in your loadout from the icon grid. The grid uses the local HellForge Editor game icon cache when
available and shows named tiles otherwise. Select the upgrade and pay at checkout. The four native loadout slots
stay unchanged. The extra appears in the mission's stratagem menu after deployment, with its normal uses. It isn't
saved to the game's loadout. Restricted extras also need requisition access, just like restricted items in the
native slots.

The upgrade costs 50 Forge Credits, 10 Common samples, 5 Rare samples and 1 Super sample by default and increases
equipped cooldowns, including the extra and Eagle rearm, by 10%. Edit `forge_credits`, `common_samples`,
`rare_samples`, `super_samples` and `other_cooldown_percent` in `extra_slot` in
[destroyer-upgrades.json](./destroyer-upgrades.json) beside the mod DLL, then restart the game. A config
without `extra_slot` still enables the other four upgrades.

An extra Eagle shares **Eagle Rearm** with any Eagles already equipped. If the mission doesn't already have Rearm,
the extra Eagle needs room for both entries. The extra's packages must also be available. If deployment cannot be
verified, the paid upgrade is retained for a later eligible solo mission in the same game session.
