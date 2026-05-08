# Analysis report: fix17 -> fix18

## Static problems found

### 1. Potential self-deadlock during mount refresh

`UpdateTrackedPlayerStatusComponent()` updated `g_player_resolve` while holding `g_state_mutex`, then called
`RefreshTrackedMountFromPlayerActor()` before the lock was released. If the mount resolver succeeded,
that path could call `UpdateTrackedMountStatusComponent()`, which also locks `g_state_mutex`.

Fix: the player update is now scoped inside a critical section, and the mount refresh runs only after the lock
has been released.

### 2. One remaining exact stamina-id check

Most of fix17 correctly changed stamina checks to `IsStaminaStatId()`, but the stat-write adjustment branch still
used `actual_type == kStaminaId`. This could skip the legacy stamina fallback id `17` even though the hook and
resolver accepted it.

Fix: that branch now uses `IsStaminaStatId(actual_type)`.

### 3. Stamina bootstrap still depended on old mount max cutoff

The prior `TryBootstrapPlayerStaminaFromEntry()` rejected stamina entries with max values above the old mount
threshold. The 1.05.01 player stamina-like entry can be high enough to overlap that threshold, so raw stamina
callbacks could still fail to bootstrap the player stamina entry.

Fix: player stamina bootstrap now prefers structural validation:
- if player health is already tracked, the stamina entry must be near that health entry;
- if player health is not tracked yet, the code subtracts the preferred `0x5A0` or legacy `0x480` offset and
  only accepts the result when it is a player-sized health entry.

## What was intentionally not changed

- Affinity hooks remain rejected/disabled.
- Horse-sized health writes are still diagnostic/locked only after mount context exists.
- Dragon/position RVAs from the previous report were not changed.
- No prebuilt binary was added.

## Build expectation

Build as `x64-Release`. The startup log should show:

```text
dllmain: source build = safe-rva-fixed-fix18-stamina-deadlock-guard
```
