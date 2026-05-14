# Fix 9: stable fix-3 baseline with damage fallback and affinity crash guard

This package is intentionally based on the last reported non-crashing source line: runtime fix 3.

## Why fixes 4 through 8 crashed

The post-fix-3 line changed the affinity-current expected bytes so the mod installed:

```text
affinity-prepare=0
affinity-current=1
```

That is an unsafe partial install. The target resolved in Crimson Desert 1.05.01 is a generic container/iterator block, not a validated affinity update routine.

## What changed here

- Kept the fix-3 scanner and hook set.
- Kept the working item gain multiplier.
- Kept the fix-3 stat-write damage path.
- Changed the health-write fallback so a valid player-like health entry can be tracked without requiring the old fixed stamina offset.
- Added an affinity all-or-nothing guard: the old two-hook affinity implementation now installs both exact sites or neither.

## Expected log

For affinity on Crimson Desert 1.05.01:

```text
hooks: affinity-prepare target did not match expected bytes ...
hooks: affinity hook pair rejected; continuing without affinity scaling
hooks: loadout ... affinity-prepare=0 affinity-current=0 ...
```

For fall/incoming damage:

```text
runtime: tracked player via health-write fallback health=0x...
runtime: adjusted stat write stat=health ...
```

Affinity scaling is not claimed fixed in this package. It is guarded off to restore stability while preserving the last non-crashing baseline.
