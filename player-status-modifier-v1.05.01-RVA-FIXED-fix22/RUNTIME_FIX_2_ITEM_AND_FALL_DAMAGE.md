# Crimson Desert 1.05.01 runtime fix 2

This patch addresses the two failures reported from the runtime log:

```text
hooks: item-gain callback TRIGGERED ... rcx=1 ...
```

with no matching `hooks: item-gain scaled to ...`, and no damage/stat-write scaling log before fall damage killed the player.

## Fixes

### Item storage/retrieval multiplier

`TryScaleItemGain()` no longer requires `IsPlayerRuntimeReady()`.

The item hook is located on the confirmed 1.05.01 quantity add:

```asm
49 01 4C 38 10    add qword ptr [r8+rdi+10h], rcx
```

For storage/retrieval flows the player status pointer may not have been discovered yet, but the item quantity is already present in `RCX`. The old runtime-ready gate prevented the multiplier from applying even though the callback fired.

Expected log after this patch:

```text
hooks: item-gain callback TRIGGERED #0 rcx=1 ...
hooks: item-gain scaled to 3
```

for `GainMultiplier=3.000`.

### Player discovery fallback

The 1.05.01 player-pointer hook can install successfully without firing before inventory or fall-damage tests. The stats hook already receives a stat entry and its owning status component, so this patch lets stats access bootstrap the player snapshot when no player marker has been captured yet.

Expected log:

```text
runtime: tracked player via stats marker=... root=... health=... stamina=... spirit=...
```

### Incoming/fall damage

The shared stat-write hook now handles `PlayerHealth`, not only `PlayerStamina`.

For health reductions it applies:

```text
Health.ConsumptionMultiplier * IncomingDamage.Multiplier
```

when `[IncomingDamage] Enabled=1`.

With your logged config:

```text
Health ConsumptionMultiplier=1.000
IncomingDamage Multiplier=0.500
```

a health write reduction should be halved. This covers fall/environment damage paths that bypass the battle-damage hook.

Expected log:

```text
hooks: stat-write callback ... tracked_player_health=...
runtime: adjusted stat write stat=health ...
hooks: stat-write adjusted ...
```

## Files changed

- `src/mod_logic.cpp`
- `src/mod_logic.h`
- `src/config.h`
- `src/hooks/player_hooks.cpp`
- `src/runtime/actor_resolve.cpp`
- `src/runtime/actor_resolve.h`
- `src/runtime/stat_logic.cpp`
