# Spend My Resources

Spend requisition from your offline profile to equip the **R-36 Eruptor** or **MS-11 Solo Silo** for one mission.
Each item costs **1,000 requisition slips** and must already be unlocked in the game.

## Installation

With the game closed, copy the packaged `SpendMyResources` folder into HellForge's `mods` folder, replacing the
previous copy if installed. Enable **Spend My Resources** on the Mods page. Requires HellForge SDK 16 and game
build `25480438`. Everyone in co-op needs the same version of the mod.

## Using the shop

1. Press **F1** on the ship to open the shop.
2. Select **Buy access** next to the item you want. Once the charge is confirmed, the button shows **Access ready**.
3. Equip the item in your loadout. Items retain their normal visibility and unlock requirements.

If an item is selected without purchased access, the mod blocks the local player's Ready input.

Access is used only when you take the item on a mission and lasts through that mission, including reinforcements.
Access is session-only and resets when the game closes; the mod does not save access to a file.
Access for items taken on a mission is consumed when the mission-end event fires, not at deployment.
Purchases are not refunded, including unused access when the game closes.

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