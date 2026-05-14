# Fix 51: special mount research diagnostics

Base: `safe-rva-fixed-fix50-stamina-spirit-diagnostics`.

## Why

Crimson Desert 1.06.00 added special rideable animals: bears, boars, wolves,
deer, mountain goats, Kuku birds, iguanas, raptors, camels, lions, and tigers.
The previous mount path was built around horses/wolves/bears that matched the
old resolved health/stamina layout.

The latest gameplay log showed one tracked ground mount profile, but later
animal-range health writes were skipped because they were not connected to the
tracked mount context.

## Change

- Added active relock diagnostics for tracked mount health/stamina:
  - read failed
  - already at lock value
  - write failed
  - successful relock
- Added stat-component rejection diagnostics so newly observed animal stat
  layouts show whether they are missing from the resolved profile or rejected by
  profile thresholds.
- Added special-mount health-write candidate diagnostics in the stat-write hook.
  These log register context for non-player, non-tracked health writes in the
  ground/special mount health range without locking them blindly.

## Expected proof

```text
dllmain: source build = safe-rva-fixed-fix51-special-mount-research
runtime: mount relock observed stat=...
hooks: special-mount health candidate ...
runtime: mount stat-component rejected ...
```

The next gameplay run should identify whether special rideable animals use a
different stat layout, different context register, or stale mount marker path.
