# Spend My Resources

Spend requisition from your offline profile to equip the restricted items below for one mission.
Items must already be unlocked in the game. The mod checks equipped loadout entries rather than querying each item's
inventory record; normal game loadout rules still apply.

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
| PLAS-39 Accelerator Rifle | 500 |
| R-6 Deadeye | 500 |
| R-63CS Diligence Counter Sniper | 500 |
| R-72 Censor | 500 |
| R/40-K Hot-Shot Marksman Rifle | 500 |
| LAS-5 Scythe | 500 |
| AR-59 Suppressor | 500 |
| LAS-17 Double-Edge Sickle | 500 |
| P-4 Senator | 500 |
| LAS-98 Laser Cannon | 500 |
| LAS-99 Quasar Cannon | 500 |
| M-1000 Maxigun | 500 |
| PLAS-45 Epoch | 500 |
| StA-X3 W.A.S.P. Launcher | 500 |
| GL-28 Belt-Fed Grenade Launcher | 500 |
| RL-77 Airburst Rocket Launcher | 500 |
| LIFT-850 Jump Pack | 500 |
| LIFT-860 Hover Pack | 500 |
| SH-32 Shield Generator Pack | 500 |
| EAT-411 Leveller | 500 |
| EXO-45 Patriot Exosuit | 500 |
| EXO-49 Emancipator Exosuit | 500 |
| EXO-51 Lumberer Exosuit | 500 |
| EXO-55 Breakthrough Exosuit | 500 |
| Orbital 380mm HE Barrage | 500 |
| Orbital 120mm HE Barrage | 500 |
| Orbital Napalm Barrage | 500 |
| Orbital Railcannon Strike | 500 |
| AC-8 Autocannon | 500 |
| A/G-16 Gatling Sentry | 500 |
| APW-1 Anti-Materiel Rifle | 500 |
| AX/AR-23 Guard Dog / gun | 500 |
| GR-8 Recoilless Rifle | 500 |
| MLS-4X Commando | 500 |

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
	- In each case spend either: (These can be stacked)
    	- Some items might not have all tiers or have all 3 tiers or just 1 tier
		- 10 common OR 1 FC = "Plus"
		- 10 Rare OR 5 FC = "Elite"
		- 10 Super OR 10 FC = "Super"
	- Primaries
    	- Plus: 2 extra mags
    	- Elite: 
	- Secondaries
	- Strats
		- Eagles
		- Orbitals
		- Support weapons
		- Backpacks
		- Vehicles
    		- FRVs
    		- Exosuits
		- Turrets
	- Spend either 20 FC or 10K Req to (limit to one of either):
		- Add one more strat slot
		- Orbitals buff: All equiped orbital strike cooldowns are halved, but other strat cooldowns are doubled.
		- Eagles buff: All equiped eagles have unlimited uses, but cooldowns are increased by 33%.
		- Defensive strats buff: All equiped defensive strats have no cooldown, but are limited to 10 uses.
	- Extremely powerful weapons and stratagems require Req slips, Samples or FC to equip
		- Primaries
			- Eruptor
			- JAR-5 Dominator
			- AR-23P Liberator Penetrator
			- PLAS-39 Accelerator Rifle
			- R-6 Deadeye
			- R-63CS Diligence Counter Sniper
			- R-72 Censor
			- R/40-K Hot-Shot Marksman Rifle
			- LAS-5 Scythe
			- AR-59 Suppressor
			- LAS-17 Double-Edge Sickle
		- Secondaries
			- Ultimatum
			- Bolt Pistol
			- P-4 Senator
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
			- LAS-98 Laser Cannon
			- LAS-99 Quasar Cannon
			- M-1000 Maxigun
			- PLAS-45 Epoch
			- StA-X3 W.A.S.P. Launcher
			- GL-28 Belt-Fed Grenade Launcher
			- RL-77 Airburst Rocket Launcher
			- LIFT-850 Jump Pack
			- LIFT-860 Hover Pack
			- SH-32 Shield Generator Pack
			- EAT-411 Leveller
			- All Exosuits
			- Orbital 380mm HE Barrage
			- Orbital 120mm HE Barrage
			- Orbital Napalm Barrage
			- Orbital Railcannon Strike
			- AC-8 Autocannon
			- A/G-16 Gatling Sentry
			- APW-1 Anti-Materiel Rifle
			- AX/AR-23 Guard Dog / gun
			- GR-8 Recoilless Rifle
			- MLS-4X Commando
			- B-1 Supply pack