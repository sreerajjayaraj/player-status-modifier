# Runtime Fix 3: Incoming/Fall Damage Health-Write Fallback

The previous 1.05.01 runtime fix made item gain scaling independent of player discovery, which is why item scaling now logs `item-gain scaled to ...`.

Incoming/fall damage still depended on resolving the player marker first. In the provided runtime log there were no `runtime: tracked player ...`, `runtime: tracked player via stats ...`, or `runtime: adjusted stat write stat=health ...` lines before the fall damage test, so the damage path had no confirmed player health entry to modify.

This fix adds a narrow fallback at the stat-write hook:

- When a health stat write decreases a stat value,
- and no player health entry has been resolved yet,
- and the stat entry looks like a normal player-sized health entry rather than a mount/dragon-sized health entry,
- and the adjacent stamina/spirit entries validate,

the mod records that health entry as the player health entry and applies the configured health/incoming damage scaling to the write.

Expected log after a fall/incoming damage event:

If the fallback cannot validate the health entry, the log will instead contain `runtime: untracked health write skipped ...`, which means the hook fired but the entry did not match the safety checks.

```text
runtime: tracked player via health-write fallback ...
runtime: adjusted stat write stat=health ...
hooks: stat-write adjusted ...
```

With the reported INI values:

```text
Health.ConsumptionMultiplier=1.000
IncomingDamage.Multiplier=0.500
```

a health loss write should be reduced by 50%.
