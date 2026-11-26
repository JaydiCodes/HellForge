# Kill rewards

Spend My Resources pays Requisition Slips for enemies the team kills in a mission. The reward is added on the
ship after the mission ends, including an abandoned mission, and saved to the mod's wallet.

## How the reward is worked out

1. Each enemy killed by a Helldiver counts once, at its value in `kill-rewards.json`.
2. The team total is split equally between the players in the session at mission end, rounded up.
3. With two or more players, each player also gets a bonus of 5% of the value of their own kills, rounded up.

For example, two players kill enemies worth 601 Req, 501 of them by you. You get 301 + 26 = 327 Req.

Only Automaton, Terminid and Illuminate units count. SEAF troops, civilians, structures and Helldivers do not.
Kills of enemies credited to no player, such as ones that die from the environment, do not count.

## The kill reward card

Each counted kill, yours or a teammate's, shows a card where the loadout cost card sits. Press F3 to move it on its
own. It fades in and counts up your projected kill reward. Five seconds after the last kill it fades out.

- The amount is a forecast. It is paid on the ship when the mission ends.
- The player count can change before the mission ends, so the paid amount can differ.
- The loadout cost card takes its place while the loadout picker is open.

Three seconds after returning to the ship, the card shows the completed mission's team and personal kills,
your team share, and your contribution bonus. It stays for ten seconds, then fades out over half a second.

- The total is the earned reward before the wallet cap, not your wallet balance.
- This summary takes the loadout cost card's place. Checkout, currency exchange and F3 layout still hide it;
  its timer continues while hidden.
- Leaving the ship dismisses the summary without replaying it on the next visit.

## Changing the values

`kill-rewards.json` maps an enemy name from HellForge's `mappings\entities.json` to a whole number from 0 to
1,000,000. Enemies that are not listed are worth 1; set a value of 0 to leave an enemy out. An unknown name, a
duplicate or an invalid value disables kill rewards for the session and is reported in the mod's log.

## Limits

- Requisition above the game's cap is lost, and the shop message says how much.
- A reward not yet paid when the game closes is lost.
- Each player's game counts the kills it is told about. In co-op, the players' totals can differ.
- The mod's log has the tally for each mission.

## Source code

In the repository, `src\gameplay\kill_rewards.c` tracks and settles mission kills.
`src\shop\frontend\kill_counter.cpp` draws the forecast card and the mission summary.
