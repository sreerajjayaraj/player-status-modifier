# Fix 18: stamina bootstrap and mount deadlock guard

Base: `safe-rva-fixed-fix17-stamina-mount`.

## Problems fixed

1. `UpdateTrackedPlayerStatusComponent()` refreshed the mount resolver while holding `g_state_mutex`.
   If the mount pointer chain resolved successfully, `RefreshTrackedMountFromPlayerActor()` could call
   `UpdateTrackedMountStatusComponent()`, which tries to take the same mutex again. That is a potential
   self-deadlock once mount resolution starts working.

2. The stamina stat-write path still used `actual_type == kStaminaId` in one place. This meant the guarded
   legacy stamina id `17` was accepted by the hook/resolver but not by the final stat-write adjustment branch.

3. `TryBootstrapPlayerStaminaFromEntry()` still rejected stamina entries whose max value crossed the old
   mount threshold. For Crimson Desert 1.05.01, the player stamina-like entry can overlap that threshold.
   The fix now uses the safer discriminator: the stamina entry must be adjacent to the tracked player health
   entry, or it must resolve a player-sized health entry by subtracting the verified stamina offset.

## Safety choices

- Type `18` is still spirit only.
- Type `19` remains the preferred 1.05.01 stamina id.
- Type `17` remains a guarded legacy fallback.
- Horse-sized health entries are still not blindly locked.
- Affinity remains untouched/disabled by the existing byte guard.
- No compiled ASI, loader DLL, game executable, PDB, OBJ, LIB, or ILK files are included.

## Expected startup log

```text
dllmain: source build = safe-rva-fixed-fix18-stamina-deadlock-guard ...
stamina-id=19 legacy-stamina-id=17 stamina-offset=0x5A0 legacy-offset=0x480 spirit-offset=0x510
```

## Expected useful runtime logs

```text
hooks: stamina-ab00 callback ...
runtime: tracked player stamina via stamina-delta fallback ...
runtime: adjusted stamina delta ...
runtime: tracked mount profile=horse ...
runtime: locked mount stamina ...
```
