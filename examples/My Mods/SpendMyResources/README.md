# Spend My Resources

Spend requisition from your offline profile to equip the restricted items below for one mission.
Items must already be unlocked in the game.

| Item | Requisition slips |
|---|---:|
| R-36 Eruptor | 1,000 |
| MS-11 Solo Silo | 2,000 |
| JAR-5 Dominator | 500 |
| AR-23P Liberator Penetrator | 250 |
| GP-20 Ultimatum | 1,000 |
| P/40-K Bolt Pistol | 750 |
| G/40-K Melta Mine | 500 |
| G-48 Giga Grenade | 750 |
| Eagle 500kg Bomb | 500 |
| RS-422 Railgun | 750 |
| Orbital Walking Barrage | 500 |
| Orbital Laser | 1,000 |
| ARC-3 Arc Thrower | 500 |

## Custom prices

Edit [`prices.json`](prices.json) beside the installed mod DLL, then restart the game. No rebuild is needed.
Change the numbers only; keep every item name exactly as supplied. Prices must be positive whole numbers
(1 to 330,382,099 Req per item). The table above shows the shipped defaults.

The file is loaded once at startup. Missing files, missing or duplicate items, unknown names, and invalid prices
stop the mod from loading and report the reason in the mod log; compiled prices are not silently substituted.
The supported item roster is fixed. Everyone in co-op should use the same price list.
Back up custom prices before replacing the mod with an update.

## Installation

With the game closed, copy the packaged `SpendMyResources` folder into HellForge's `mods` folder, replacing the
previous copy if installed. Enable **Spend My Resources** on the Mods page. Requires HellForge SDK 16 and game
build `25480438`. Everyone in co-op needs the same version of the mod.

## Using the shop

1. Equip your loadout. Items retain their normal visibility and unlock requirements.
2. Press **Ready**. If an equipped restricted item needs access, checkout opens instead of readying up.
3. Review the equipped restricted items, their individual costs, and the total due. Existing access is not charged again.
4. Select **Purchase access** to pay the total in one transaction. After success, checkout closes and readies you
   automatically. The game's normal loadout checks still apply (including required stratagem slots).

Close checkout with **Escape** or its X to change your loadout without purchasing. F1 does not open the mod.

Access is used only when you take the item on a mission and lasts through that mission, including reinforcements.
Access is session-only and resets when the game closes; the mod does not save access to a file.
Access for items taken on a mission is consumed when the mission-end event fires, not at deployment.
Purchases are not refunded, including unused access when the game closes.

Purchases deduct from the running game's inventory first, then subtract the same amount from the active offline
profile JSON. Access is granted only after both steps are verified. A partial failure stops further purchases and
is reported in the shop. This does not refresh the backend's in-memory profile; later backend saves may overwrite
the file edit.

## Currency exchange (F2)

On the ship, press **F2** to open the separate currency exchange menu. Choose the currency to give, the currency to
receive, and the number of batches, then select **Exchange**. The menu shows all five balances, the exact cost and
return, and the running game's destination cap. Close with F2, Escape or X.

| Currency | Points per unit |
|---|---:|
| Requisition slips | 1 |
| Common sample | 50 |
| Rare sample | 100 |
| Super sample | 200 |
| Forge Credit | 500 |

All pairs work in both directions. Exchanges use exact whole-unit batches: for example, 50 Req = 1 Common,
2 Common = 1 Rare, and 5 Super = 2 Forge Credits. Any unused source currency stays in your balance; there are no
fees or rounding losses. Exchanges that exceed your balance or the destination cap are rejected, not clipped.
Forge Credits use HellForge's existing wallet, not a new currency.

Exchanges run only on the ship, one transaction at a time (shared with access purchases). The mod removes the source
and adds the destination with native inventory functions, verifies both changes, then updates both amounts in the
active offline profile JSON in one verified write. A missing destination profile entry is created. A partial failure
disables both exchanges and access purchases and reports the error; the mod never automatically retries.
The same backend in-memory profile limitation described above applies. No backend changes are required.

F2 does not open access checkout or trigger Ready. Checkout still opens only when Ready is blocked; it takes priority
over the exchange menu. Leaving the ship closes the exchange menu and cancels queued exchanges.

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
		- Primaries
			- Eruptor
			- JAR-5 Dominator
			- AR-23P Liberator Penetrator
		- Secondaries
			- Ultimatum
			- Bolt Pistol
		- Throwables
			- Melta Mine
			- Giga grenade
		- Stratagems
			- Solo Silo
			- 500 Kg
			- RS-422 Railgun
			- Orbital Walking Barrage
			- Orbital Laser
			- ARC-3 Arc Thrower