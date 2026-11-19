## Prototype: in-game requisition shop

From a HellForge source checkout, run `.\scripts\Build-Editor.ps1 -SpendMyResources`, then copy the built
`artifacts\SpendMyResources` folder into your installation's `mods` folder (replace the earlier prototype if installed).
Enable **Spend My Resources** on the Mods page. The shop ships as a standalone DLL and does not replace
HellForge's F8 editor. The DLL incorporates HellForge's GPL-3.0 renderer and Dear ImGui; the build sources
are in `native\editor`, `examples\My Mods\SpendMyResources`, and `scripts\Build-Editor.ps1` in the same checkout.

**Version 0.7 uses the existing HellForge backend and session runner.** It registers a fake weapon copied from the
Liberator Carbine and purchases a copied attachment whose catalogue price is 1,000 requisition. This hidden token uses
the normal `/Progression/buy/v2` route; the mod tracks temporary access separately. The fake weapon is hidden from the
session inventory, never equips, and does not change the player's underlying unlocks.

In the session's
`mod-jaydicodes.spend-my-resources.log`, look for `Mission start loadout:` followed by the primary, secondary and
throwable loadout IDs and a line for each stratagem slot (numeric stratagem type).

If the local loadout is not ready at mission start, the mod retries once on local deployment. It does not record
other players' loadouts, and a reinforcement does not produce a second snapshot. This version requires SDK 16
and game build `25480438`. Armor and boosters are not included in the loadout log.

The mod is marked as shared gameplay: everyone in co-op needs the same build. **Press F1 while on the ship (before
deployment)** to open the test shop inside the game. It uses the game's DirectX render surface, not a separate Windows
panel. Press F1 again, Escape, or use the close button to dismiss it. Close any other overlay before opening the shop.

The shop shows the player's **actual in-game requisition balance**, with no simulated starting funds. Click **Buy access**
to deduct 1,000 requisition from the saved offline profile and the in-game wallet. Access is granted only after the backend
confirms the saved charge. The button then shows **Access ready**. A pending purchase disables other purchases. If a
purchase reply is lost, its outcome is unknown: the shop stops further purchases for this session rather than retrying
and possibly charging twice. Check the saved profile before restarting.

The mission-start log consumes purchased access only for listed items actually selected. If the read is deferred to local
deployment, access is consumed then. Consumed access remains effective throughout that mission, including reinforcements,
and is revoked when the mission ends. Unused access carries over to later missions in the same session, but is not saved
across restarts. Restarting does **not** undo the saved requisition deduction.

| Item | Price |
|---|---:|
| R-36 Eruptor | 1,000 Requisition |
| MS-11 Solo Silo | 1,000 Requisition |

Without access, the mod removes the two items' ownership using the game's own inventory functions, the same mechanism
HellForge uses to take removed mods' content out of the selection UI. Depending on the picker, an unowned item is locked
or absent; this does not add custom lock artwork. Buying access restores the item's original owned amount. Access does
not bypass an underlying unlock: **Not owned** means the player must unlock the item normally first.

Already-selected unpurchased items are removed from the local ship loadout while the loadout picker is closed: Eruptor
falls back to Liberator and Solo Silo is removed from the slots. Other players' loadouts are untouched. The mod does not
edit saved weapon/stratagem unlock ownership, refund purchases, or charge again on deployment.

For testing, look for `Purchase saved: ... (1000 Requisition; ... remaining in game)` in the mod log, and compare the
profile inventory's requisition entry (`mixId: 3608481516`) before and after purchase. The backend audit records
`POST /Progression/buy/v2` with `purchase_saved`. Verify the game's own wallet and both selection screens,
then restart to confirm the deduction persists. The automated fixtures use synthetic game memory, not a running game.

### Purchase token and tests

The mod finds the session's local backend from its `backend\ready.json`. Before sending a purchase it checks the served
catalogue contains the fake attachment at exactly 1,000 requisition, then verifies the normal purchase reply. Each
successful purchase also saves one fake attachment in the profile; the access itself is kept only by the mod. Existing
token ownership does not prevent repeat purchases. There is no backend-side transaction ID, so an unknown result cannot
be safely retried automatically.

Add `-Test` to the build command to run the real-HTTP backend/native shop integration fixture, then Direct3D 12 and 11
overlay fixtures (these last two require a Windows desktop and GPU). The integration fixture checks saved deductions,
lost-reply handling, access lifetime, local-only loadout cleanup, insufficient funds and refused purchases. The overlays
check opening/closing and that a missing game inventory cannot make a purchase.

Backend-only checks: `dotnet run --project tests\HellForge.LocalBackend.Tests -- --requisition`.

## Design ideas

- **Spend samples, Req or FC (Forge Credits) to empower weapons / strats for one operation**
	- Remove resource limits
	- Resources exchange
		- Req = 1 Point
		- Common = 50 Points
		- Rare = 100 Points
		- Super = 200 Points
		- FC = 500 Points
	- Also increase Req reward 2 times all sources
	- In each case spend either
		- 10 common OR 5K Req OR 1 FC
		- 10 Rare OR 10K Req OR 5 FC
		- 10 Super OR 25K Req OR 10 FC
	- Primaries
	- Secondaries
	- Strats
		- Eagles
		- Orbitals
		- Support weapons
		- Backpacks
		- Vehicles
		- Turrets
	- Spend either 10 FC, 10K Req, 50 Common, 25 Rare or 10 Super to (limit to one of each):
		- Add one more strat slot
		- Add one more booster
	- Extremely powerful weapons and stratagems require Req slips, Samples or FC to equip
		- Need either:
			- 1 FC, 1K Req, 10 Common, 5 Rare or 1 Super
		- Primaries
			- Eruptor
			- Dominator
		- Secondaries
			- Ultimatum
			- Bolt Pistol
		- Throwables
			- Melta Mine
		- Stratagems
			- Solo Silo
			- 500 Kg