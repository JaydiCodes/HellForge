# Spend My Resources

Spend requisition from your offline profile to equip the restricted items below for one mission.
Items must already be unlocked in the game. The mod checks equipped loadout entries rather than querying each item's
inventory record; normal game loadout rules still apply.

## Custom prices

Edit [`prices.json`](prices.json) beside the installed mod DLL, then restart the game. No rebuild is needed.
Change the numbers only; keep every item name exactly as supplied. Prices must be positive whole numbers
(1 to 89,478,485 Req per item). The table above shows the shipped defaults.

The file is loaded once at startup. Missing files, missing or duplicate items, unknown names, and invalid prices
stop the mod from loading and report the reason in the mod log; compiled prices are not silently substituted.
The supported item roster is fixed. Everyone in co-op should use the same price list.
Back up custom prices before replacing the mod with an update.

## Installation

With the game closed, copy the packaged `SpendMyResources` folder into HellForge's `mods` folder, replacing the
previous copy if installed. Enable **Spend My Resources** on the Mods page. Requires HellForge SDK 16 and game
build `25480438`. Everyone in co-op needs the same version of the mod.

## Using the shop

1. Equip your loadout. A read-only panel shows your total requisition cost, the price of each equipped restricted
   item, any access you've already paid for, and your available balance. It updates as you change gear.
   Items retain their normal visibility and unlock requirements.
2. Press **Ready** to open **Destroyer Upgrades**. Choose **Select upgrade** or **Skip upgrade** to continue.
   Selecting an upgrade does not charge you. Checkout opens if you selected an upgrade or an equipped item needs
   access. Otherwise, the game continues readying up normally.
3. Review the individual charges and totals in Requisition and Forge Credits. Existing access is not charged again.
4. Select **Purchase** to pay for the selected upgrade, restricted gear access, or both. After success, checkout closes and readies you
   automatically. The game's normal loadout checks still apply (including required stratagem slots).

Close checkout with **Escape** or its X to change your loadout without purchasing. This clears the unpaid upgrade
selection. F1 does not open the mod.
The upgrade and checkout windows block new mouse presses from reaching the game but let left-button releases through,
so the click that opens the window cannot leave the game holding the button down.

Access is used only when you take the item on a mission and lasts through that mission, including reinforcements.
Access is session-only and resets when the game closes; the mod does not save access to a file.
Access for items taken on a mission is consumed when the mission-end event fires, not at deployment.
Purchases are not refunded, including unused access when the game closes.

Purchases deduct from the running game's inventory first, then subtract the same amount from the active offline
profile JSON in one write, even when two currencies are used. Access and the selected upgrade are granted only after
both steps are verified. A partial failure stops further purchases and
is reported in the shop. This does not refresh the backend's in-memory profile; later backend saves may overwrite
the file edit.

## Destroyer upgrades

Select a square icon above the description to preview an upgrade. Orbital, Eagle, Defensive, Support, and
[Extra Stratagem Slot](./extra-stratagem-slot.md) are available in solo play. The extra stratagem is granted after
deployment, without adding a fifth slot to the main loadout.

**Known issue: Orbital Laser in the Extra Stratagem Slot.** HellForge's asset catalogue is missing the Orbital Laser
package, so checkout is cancelled without deducting any resources. Until HellForge fixes this, equip Orbital Laser in
one of the four normal stratagem slots in your main loadout instead.

The picker uses the local HellForge Editor icon cache (`mods\hellforge-editor\game-icons.bin`) when available:
Orbital Precision Strike, Eagle Airstrike, Gatling Sentry, and Supply Pack. If a sprite is missing, the picker uses
a drawn fallback. Extra Stratagem Slot always uses a plus icon. No game art is shipped with the mod.

| Upgrade | Default benefit | Default trade-off |
|---|---|---|
| Orbital | Equipped orbital strikes and barrages have 66% shorter cooldowns. | Other equipped cooldowns and Eagle rearm are 50% longer. |
| Eagle | Eagles rearm in 30 seconds. | Equipped Eagles take 2 seconds longer to call in and have a 20-second cooldown between uses. Other equipped cooldowns are 50% longer. |
| Defensive | Equipped defensive stratagems have 66% shorter cooldowns and unlimited uses. | Other equipped cooldowns and Eagle rearm are doubled. |
| Support | Equipped support stratagems have 66% shorter cooldowns and unlimited uses. | Other equipped cooldowns and Eagle rearm are doubled. |

Resupply and objective-granted stratagems such as the objective Hellbomb are unchanged. Nothing on the destroyer
itself changes. Co-op players can skip the upgrade screen and continue to Ready or checkout normally.

Each upgrade costs **50 Forge Credits by default**, deducted at checkout, not on the upgrade screen. The effects last one solo mission.
You can buy the upgrade only once before a mission. If you change your loadout before deployment, it applies to the
stratagems you actually take. A paid upgrade stays available in the current game session until it applies to a mission;
it is not saved or refunded when you quit. Escape or X closes the upgrade screen without continuing to Ready.

### Configuring upgrade effects and prices

Edit [`destroyer-upgrades.json`](destroyer-upgrades.json) beside the installed mod DLL and restart the game.
No rebuild is needed. The picker and checkout use the same settings as the mission effects. Back up your edited
file before redeploying the mod, since deployment copies the shipped config over the installed one.

| Setting | Meaning |
|---|---|
| `forge_credits` | Cost for that upgrade (a positive whole number). |
| `cooldown_percent` | Orbital, Defensive, and Support: percentage change to each matching equipped stratagem's cooldown. `-66` leaves 34% of its cooldown. |
| `other_cooldown_percent` | Percentage change to other equipped cooldowns. `50` makes them 50% longer; `0` leaves them unchanged. Also affects Eagle rearm on non-Eagle upgrades. |
| `cooldown_seconds` | Eagle only: fixed cooldown between uses for equipped Eagles. `0` removes it. |
| `call_in_extra_seconds`, `rearm_seconds` | Eagle only: seconds added to call-in time and the fixed Eagle rearm time, respectively. |

If you reuse an older config, replace `cooldown_seconds` with `cooldown_percent` (for example, `-66`) in
`defensive` and `support`, and remove any remaining `uses` entries. Keep `cooldown_seconds` in `eagle`.

Keep the four required sections and their settings. Percentages accept `-100` to `10000`; times accept `0` to `86400`
seconds. If the file is missing or invalid, the mod logs the error and disables upgrade selection; gear checkout
and currency exchange still work. The optional `extra_slot` section is described in the
[Extra Stratagem Slot guide](./extra-stratagem-slot.md).

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
| Forge Credit | 750 |

All pairs work in both directions. Exchanges use exact whole-unit batches: for example, 50 Req = 1 Common,
2 Common = 1 Rare, and 15 Super = 4 Forge Credits. Any unused source currency stays in your balance; there are no
fees or rounding losses. Exchanges that exceed your balance or the destination cap are rejected, not clipped.
Forge Credits use HellForge's existing wallet, not a new currency.

Exchanges run only on the ship, one transaction at a time (shared with access and upgrade purchases). The mod removes the source
and adds the destination with native inventory functions, verifies both changes, then updates both amounts in the
active offline profile JSON in one verified write. A missing destination profile entry is created. A partial failure
disables both exchanges and access purchases and reports the error; the mod never automatically retries.
The same backend in-memory profile limitation described above applies. No backend changes are required.

F2 does not open upgrades or access checkout, or trigger Ready. The Ready screens take priority over the exchange
menu. Leaving the ship closes the exchange menu and cancels queued exchanges.

AGENTS: DO NOT MODIFY BELOW TEXT

## Design ideas

- **Spend samples, Req or FC (Forge Credits) to empower weapons / strats for one operation**
	- Remove resource limits
	- Resources exchange
		- Req = 1 Point
		- Common = 50 Points
		- Rare = 100 Points
		- Super = 200 Points
		- FC = 750 Points
	- In each case spend either: (These can be stacked)
    	- Some items might not have all tiers or have all 3 tiers or just 1 tier
		- 50 common = "Plus"
		- 25 Rare = "Elite"
		- 5 Super = "Super"
    - Helldiver:
        - Plus: 1 of either
            - No primary weapon, but +50% secondary max ammo
            - No secondary, but +33% primary max ammo
            - -50% throwables, -50% stims, +33% primary max ammo, +50% secondary max ammo
        - Elite: 1 of either:
            - +25% armor rating, -10% max ammo primary and secondary
            - +25% sprint speed, -10% stamina
            - +25% stamina, -10% sprint speed
        - Super: 1 of either:
            - When reaching 10% HP, auto stim. Only once every 30 seconds, -2 stims.
            - All injuries auto fix after 2 seconds, -10% movement speed
	- Primaries & Secondaries
    	- Plus: +2 Mags
    	- Elite: 2 choices
        	- Direct hit AP +1, Direct hit Durable damage -25%
        	- Direct hit AP -1, Direct hit Durable damage +50%
      	- Super: 1 of either
        	- Assault rifles:
            	- Explosive shrapnel rounds, -1 AP
            	- Explosive incindiary rounds, -10% damage, -1AP
	- Strats
		- Eagles
    		- Elite: +1 Use
    		- Super: 3 V formation eagles, +10s call-in time
  		- Orbitals
    		- Railcannon:
        		- Super: Fire another shot after the first one, 1 second later.
  		- Turrets:
    		- Plus: +10% Ammo
    		- Elite: +25% HP
	- **Destroyer upgrades** 
    	- Spend 50 FC + 10 common + 5 rare + 1 super to upgrade destroyer (limit to one of either):
    		- Add one more strat slot
        		- Debuff: All equiped stratagems have a 10% increased cooldown (Includes Eagle rearm time)
    		- Orbitals upgrade (Use precision strike icon): 
        		- Buff: All equiped orbitals cooldowns are reduced by 66%
        		- Debuff: All other equiped stratagem cooldowns increased by 50% (Includes Eagle rearm time)
    		- Eagles upgrade  (use airstrike icon):
        		- buff: Eagles have a 30 second rearm cooldown 
        		- debuff: Eagles call in time is increased slightly (2 seconds) and their use cooldown is increased to 20 seconds.
        		- debuff: All other equiped stragem cooldowns increased by 50%
    		- Defensive upgrade (Use gatling sentry icon)
        		- buff: All equiped defensive strats have no cooldown, 
        		- Debuff: defensive strats Limited to 10 uses.
        		- Debuff: All other equiped strategem cooldowns increased by 50% (Includes Eagle rearm time)
    		- Support strats buff: 
        		- buff: All equiped Support strats have no cooldown, 
        		- Debuff: Support strats Limited to 10 uses.
        		- Debuff: All other equiped strategem cooldowns increased by 50% (Includes Eagle rearm time)
	- Extremely powerful weapons and stratagems require Req slips to equip
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
			- B-1 Supply Pack