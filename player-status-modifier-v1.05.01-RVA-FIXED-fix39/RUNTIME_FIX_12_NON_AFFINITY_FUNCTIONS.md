# Runtime Fix 12 - Non-affinity runtime functions from stable fix-11B baseline

Base: fix-11B, which already confirmed:
- ASI/INI load
- item gain scaling
- incoming/fall damage reduction
- outgoing damage stat-write scaling
- affinity crash guard with affinity-current disabled when prepare fails

Affinity is intentionally untouched in this build.

## Changes in fix-12

### Durability

The 1.05.01 logs showed durability delta callbacks like:

```text
hooks: durability-delta callback ... current=65526 delta=1
```

The old logic only treated negative deltas as consumption, so positive wear-counter deltas were ignored.  Fix-12 treats any non-zero durability delta as a consumption candidate and zeros it when `[Durability] ConsumptionChance` says consumption should be skipped. It also removes the player-runtime-ready gate so durability works even when the mod only discovered the player through the health-write fallback.

Expected logs when `ConsumptionChance=0`:

```text
runtime: skipped durability delta entry=0x... current=... original=1 chance=0.00
hooks: durability-delta adjusted entry=0x... final_delta=0
```

### Spirit

Fix-11B saw player spirit callbacks, but they could be ignored when the player was discovered only through health-write fallback and the full actor/root resolver was not complete. Fix-12 allows the spirit hook to use the health-nearby fallback and apply `[Spirit] ConsumptionMultiplier` / `HealMultiplier` once the spirit entry is known.

Expected logs:

```text
hooks: spirit-delta callback entry=0x... delta=...
runtime: adjusted spirit delta entry=0x... old=... final=...
hooks: spirit-delta adjusted entry=0x... final-delta=...
```

### Stamina

The old AB00 stamina path was left as mount-only and returned before player stamina could be discovered when the full actor/root resolver had not completed. Fix-12:
- removes the premature tracked-stamina gate from the AB00 callback,
- tries a narrow player-stamina fallback near the tracked player health entry,
- restores player stamina delta scaling using `[Stamina] ConsumptionMultiplier` / `HealMultiplier`,
- keeps mount stamina lock support.

Expected logs:

```text
runtime: tracked player stamina via stamina-delta fallback stamina=0x...
runtime: adjusted stamina delta entry=0x... old=... final=...
hooks: stamina-ab00 adjusted entry=0x... final-delta=...
```

### Mount lock

Fix-12 keeps the existing tracked-mount lock path and adds conservative fallback handling for large mount-like health/stamina entries:
- health entries with large max values can be locked by the stat-write hook,
- stamina entries with large max values can be locked by the AB00 delta hook.

Expected logs:

```text
runtime: locked mount-like health write entry=0x... old=... requested=... final=... max=...
runtime: locked mount-like stamina delta entry=0x... old_delta=... current=... lock=... max=... final_delta=...
```

## Unchanged

- Affinity is still guarded off; it is not fixed in this build.
- Item gain, incoming damage, and outgoing damage logic remain based on fix-11B.
- Position control is not changed.
- Dragon limit hooks are not changed.
