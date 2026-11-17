# JedsDead's UnBalance Pass

In solo play, the code requests Blunt-Force Mitigation as the local player's second armor passive, while leaving the
worn armor's primary passive in place. The existing stats file sets that perk's knockdown multiplier to `0.3`. The
passive also supplies its impact-damage bonus and replaces any passive already in the second slot. The armor's own
bonus takes precedence if it also has knockdown resistance; for example, Rock-Solid retains its own multiplier.

The override waits in multiplayer and resumes when playing alone. Its status and any failure appear in the mod's
session log. Perk-value changes have not been verified in gameplay; test knockdown and collision behavior in a mission.
Adding code makes this a shared gameplay mod, so friends must have the same mod files.
