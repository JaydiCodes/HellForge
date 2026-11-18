## Prototype: in-game test shop

From a HellForge source checkout, run `.\scripts\Build-Editor.ps1 -SpendMyResources`, then copy the built
`artifacts\SpendMyResources` folder into your installation's `mods` folder (replace the earlier prototype if installed).
Enable **Spend My Resources** on the Mods page. The shop ships as a standalone DLL and does not require replacing
HellForge's F8 editor or backend. The DLL incorporates HellForge's GPL-3.0 renderer and Dear ImGui; the build sources
are in `native\editor`, `examples\My Mods\SpendMyResources`, and `scripts\Build-Editor.ps1` in the same checkout.

In the session's
`mod-jaydicodes.spend-my-resources.log`, look for `Mission start loadout:` followed by the primary, secondary and
throwable loadout IDs and a line for each stratagem slot (numeric stratagem type).

If the local loadout is not ready at mission start, the mod retries once on local deployment. It does not record
other players' loadouts, and a reinforcement does not produce a second snapshot. This version requires SDK 16
and game build `25480438`. Armor and boosters are not included in the loadout log.

The mod is marked as shared gameplay: everyone in co-op needs the same build. **Press F1 while on the ship (before
deployment)** to open the test shop inside the game. It uses the game's DirectX render surface, not a separate Windows
panel. Press F1 again, Escape, or use the close button to dismiss it. Close any other overlay before opening the shop.

Each session starts with **5,000 test Requisition** and no temporary access. Click **Buy test access** next to an
item to buy one grant; the button then shows **Access ready**. The test balance is separate from the player's profile.
The mission-start log reports the total price of all listed items in the local loadout and consumes any test access for
those items. If the read is deferred to local deployment, access is consumed then. Unused access carries over to the next
mission in the same game session; it is not saved across restarts.

| Item | Price |
|---|---:|
| R-36 Eruptor | 1,000 test Requisition |
| MS-11 Solo Silo | 1,000 test Requisition |

These are simulated purchases: no real Requisition is deducted, neither item is hidden or blocked, and saved unlocks
are unchanged.

To run the Direct3D 12 and 11 shop fixtures on a Windows desktop with a GPU, add `-Test` to the build command.

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