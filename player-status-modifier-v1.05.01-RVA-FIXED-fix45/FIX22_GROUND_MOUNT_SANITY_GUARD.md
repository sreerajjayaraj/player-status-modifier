# Fix 22 Ground Mount Sanity Guard

Base: fix21 `safe-rva-fixed-fix21-ground-mount-profile`.

Reason for this patch:
- The Silverfang-only log proved the wolf can be discovered as a ground mount, but later active relock accepted stale or mismatched entries.
- Bad examples included a player-sized ground profile (`health_max=1650000`) and a bogus stamina entry with an impossible max value.
- The resolver was choosing health and stamina independently when scanning a status root, which allowed mixed stat entries from unrelated/recycled components.

Changes:
- Added sane upper bounds for ground-mount and dragon-mount maxima.
- Required health and stamina entries to use the verified Crimson Desert 1.05.01 layout (`health + 0x5A0`) or the legacy guarded layout (`health + 0x480`).
- Changed the mount stat root scan to accept only adjacent health/stamina pairs, instead of independently picking the largest health and largest stamina candidates.
- Active mount relock now re-resolves the tracked marker/root before each write and resets the tracked mount if the snapshot no longer validates.
- Mount damage lock revalidates the tracked mount first and no longer heals by inversion when the health entry cannot be safely read.
- Removed a lock re-entry path in `UpdateTrackedMountStatusComponent` by moving relock after the state mutex is released.

Expected new startup line:

```text
dllmain: source build = safe-rva-fixed-fix22-ground-mount-sanity-guard
```

Expected log behavior:
- Silverfang-style entries such as `health_max=300000 stamina_max=30000` or `health_max=450000` with adjacent stamina may track as `profile=ground`.
- Player-sized stale entries around `health_max=1650000` should be rejected.
- Impossible stamina maxima and non-adjacent health/stamina pairs should not be actively relocked.
