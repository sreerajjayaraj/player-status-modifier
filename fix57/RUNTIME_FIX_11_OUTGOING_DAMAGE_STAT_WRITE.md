# Runtime Fix 11 - Outgoing Damage Stat-Write Path

Base: `runtime-fix9-stable-from-fix3`

Fix 9 is the confirmed stable baseline for Crimson Desert 1.05.01:
- item gain scaling works
- incoming/fall damage scaling works
- the old unsafe affinity-current hook is rejected

The 2026-05-07 log showed non-player health writes such as:

```text
runtime: untracked health write skipped entry=... old=50000 requested=46000 max=50000
```

Those entries are not the player because the tracked player health entry is `max=1650000`.
They are therefore treated as candidate outgoing-damage targets.

This fix adds outgoing damage scaling to the stat-write path for untracked health entries only when:
- `[OutgoingDamage] Enabled=1`
- `Multiplier != 1.0`
- the entry is a health stat
- the write is damage (`requested < old`)
- the entry is not the tracked player health entry
- the player health entry is already known and readable

It does not change the working incoming/fall damage path and does not re-enable affinity.
