# Currency persistence

This replaces the older direct profile-edit behavior described in the README. Spend My Resources keeps its own
balance for each of the five currencies. The first session seeds the wallet from the inventory HellForge loads.
Subsequent sessions restore the saved wallet for that profile. Checkout and exchanges save all five balances
together; gear access and destroyer upgrades remain session-only.

Equipment upgrades read their prices and weapon values from `equipment-upgrades.json` beside the mod DLL.
Edit a weapon's `super` section to change its sample price or an option's damage, penetration and explosion values.
Other price keys are `requisition`, `common_samples`, `rare_samples` and `forge_credits`; omitted currencies cost zero.
Keep every weapon, tier, option and stat key present. Invalid or missing configuration disables Equipment upgrades
and reports the reason in the mod log.

Restart the game session after editing; JSON changes need no rebuild. The Equipment tab displays the configured
values, and its price is added to other checkout charges. An upgrade lasts one mission with that weapon.

In the F2 exchange, choose the currency to give on the top icon row and the currency to receive on the bottom row.
The arrow shows the direction. Select the number of whole-unit batches, then press **Exchange**.

The cost overview appears at the top center by default, or at a saved position. It reads the current selections
when the loadout picker opens and updates as they change. If the picker is still loading, it shows the cost as
unavailable instead of showing the previous mission's gear.

The mod saves actual changes in the loaded game inventory, including mission payouts and Forge Credit pickups.
It does not add rewards a second time from the backend's save. Caps still limit the amount that can be held
at any one time, not the total that can be earned or spent.

The wallet works only while this mod is enabled. The backend's `profile.json` is untouched by the mod, so normal
game shops still approve purchases against the backend's balance. Changes made with the mod disabled, or account
resyncs in the launcher, do not reset its saved wallet.

Wallet files are in the HellForge installation's `userdata\environment\Local\SpendMyResources` folder, in separate
`wallet-*.json` files. The `profile` field identifies the associated offline profile. To re-seed a wallet from that
profile's current balances, fully end the HellForge session and move the corresponding wallet file out of the folder
before relaunching. If saving or restoring fails, the mod reports the error and disables its shop for that session
instead of silently replacing the saved wallet.
