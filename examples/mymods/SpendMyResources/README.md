# Spend My Resources

Spend My Resources (SMR) adds paid access to restricted equipment, one-mission upgrades, mission kill rewards, and
currency exchange to HellForge. It keeps a separate wallet for each offline profile.

Items must already be unlocked. Normal game visibility and loadout rules still apply. Paid access lasts for the
mission; purchased upgrades apply to one mission.

[SMR has modded content support](#modded-content)

## Requirements

- HellForge mod SDK 16 and game build `25480438`.
- In co-op, every player should use the same mod version and configuration.
- Orbital Laser cannot currently be granted through Extra Stratagem Slot. Use a native stratagem slot for it.

## Installation

1. Close the game.
2. Copy the packaged `SpendMyResources` folder into HellForge's `mods` folder.
3. Enable **Spend My Resources** on HellForge's Mods page.

## Using the mod

1. Equip your loadout. The cost panel updates with restricted equipment charges.
2. Press **Ready** to review upgrades. Selecting an upgrade previews it but does not charge you.
3. If an upgrade or restricted equipment needs payment, checkout opens. Review charges and select **Purchase**.
4. Press **Escape** or **X** to close checkout without paying.
5. On the ship, press **F2** to exchange currencies.
6. Press **F3** to arrange UI panels. Drag panels, then press **F3** or **Escape** to save.

Your wallet persists per offline profile. Unused access and upgrade selections are lost when the game closes.
The native four-slot loadout remains unchanged.

## Configuration

Edit these files beside the installed mod DLL, then restart the game. Back up edits before updating SMR. Invalid
configuration disables the affected feature and is reported in `mod-jaydicodes.spend-my-resources.log`.

| File | Controls |
|---|---|
| `prices.json` | Restricted equipment prices. |
| `destroyer-upgrades.json` | Upgrade prices and effects. |
| `helldiver-upgrades.json` | Helldiver upgrade prices and effects. |
| `equipment-upgrades.json` | Equipment upgrade prices and weapon effects. |
| `kill-rewards.json` | Requisition rewards for enemies. |
| `modded-content.json` | Local support for equipment and enemies from other mods. |

Keep supplied names and JSON keys unchanged. Restart the game session after editing.

## Currency wallet

SMR maintains balances for Requisition, Forge Credits, Common samples, Rare samples, and Super samples. The first
session seeds the wallet from HellForge's inventory. Later sessions restore the saved wallet for that profile.

**Exchange** 

The F2 exchange chooses a currency to give and a currency to receive. Select a whole-unit batch count, then choose

Exchanges need enough balance and room under the destination cap.

SMR saves actual wallet changes in the loaded game inventory, including mission payouts and Forge Credit pickups.
It does not add backend rewards again. Game caps limit holdings but not lifetime earnings or spending.

The mod wallet works only while SMR is enabled. The backend `profile.json` is untouched. Normal game shops continue
to use the backend balance. Changes made with SMR disabled or account resyncs do not reset the saved wallet.

Wallet files are separate `wallet-*.json` files in:

```text
<HellForge installation>\userdata\environment\Local\SpendMyResources
```

The `profile` field identifies the offline profile. To reseed a wallet, end the HellForge session, move the
corresponding wallet file out of the folder, and relaunch. If saving or restoring fails, SMR reports the error and
disables its shop for that session instead of replacing the saved wallet.

## Kill rewards

Kills by Helldivers add Requisition Slips on mission end and save the result to the SMR wallet.

1. Each counted kill uses its value in `kill-rewards.json`.
2. The team total is split equally between session players and rounded up.
3. With two or more players, each player gets a bonus based on the percentage of kills they contributed towards.
4. Abandoned missions pay 20% of that reward, rounded down.

For base-game enemies, `kill-rewards.json` maps names from `mappings\entities.json` to whole numbers from 0 to
1,000,000. A base-game enemy not listed is worth 1. Set a listed enemy to `0` to exclude it. Misspellings, duplicate
entries, or invalid values disable kill rewards for the session and are logged.

## Modded content

Mod authors can ship support with a `spend-my-resources.json` besides their `hellforge-mod.json`:

```json
{
  "enemies": { "brute": 25 },
  "weapons": { "rail_rifle": 750 },
  "stratagems": { "pelican_gunship": 1500 }
}
```

Use registered names, not display labels or numeric IDs. Include only categories you need. SMR reads the file when
both mods load; no SMR dependency or code change is needed.

The example rewards 25 Requisition for each Brute kill, charges 750 Requisition for the Rail Rifle, and charges
1,500 Requisition for the Pelican Gunship. It does not create equipment upgrade paths.

To add support locally for a mod that lacks the file:

1. Enable SMR and the content mod.
2. Open SMR's log in the launcher Console.
3. Find `Uncatalogued mod content:` and copy registered weapon, stratagem, or enemy names.
4. Open `modded-content.json` beside `spend_my_resources.dll`.
5. Add a group under the content mod's manifest `id`, preserving existing entries.
6. Save and restart the game with both mods enabled.

Example local catalog:

```json
{
  "author.arsenal": {
    "enemies": { "author.arsenal:brute": 25 },
    "weapons": { "author.arsenal:rail_rifle": 750 },
    "stratagems": { "author.arsenal:pelican_gunship": 1500 }
  }
}
```

Names containing `/` or `:` are used as written. Otherwise, a short name belongs to the owning mod. Enemy rewards range
from 0 to 1,000,000. Weapon and stratagem prices range from 1 to 4,194,303 Requisition.

SMR uses a fixed table for 1,024 restricted items, of which 249 are built-in entries, plus a separate table for up to 1,024
modded enemies. It logs and skips entries it cannot resolve or fit.

Overrides use this priority:

1. Author `spend-my-resources.json`
2. SMR `modded-content.json`
3. Player `prices.json` or `kill-rewards.json`

The author file overrides SMR's catalog. Player price and reward files override both and can change values, but they
cannot register missing content.

An `entity (possible enemy)` entry is only a candidate. Confirm it is an enemy before assigning a reward.
