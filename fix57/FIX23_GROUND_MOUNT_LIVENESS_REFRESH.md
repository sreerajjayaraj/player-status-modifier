# Fix 23 Ground Mount Liveness Refresh

Base: fix22 `safe-rva-fixed-fix22-ground-mount-sanity-guard`.

Reason for this patch:
- The new Silverfang log showed successful wolf/ground-mount detection:

```text
runtime: tracked mount profile=ground ... health_max=300000 ... stamina_max=30000
```

- Later in the same run, non-player health candidates reported `mount_tracked=0`.
- That means the old player-to-mount pointer chain did not keep refreshing this ground mount, so the generic stale-component cleanup dropped the tracked mount even though the marker/root/stat snapshot could still be revalidated safely.

Changes:
- Successful active mount marker revalidation now refreshes `g_mount_last_seen_tick`.
- Before stale cleanup resets a tracked mount, the resolver performs one just-in-time active relock/revalidation pass.
- If that revalidation succeeds, the mount remains tracked; if it fails, stale cleanup can still reset the mount safely.

Expected new startup line:

```text
dllmain: source build = safe-rva-fixed-fix23-ground-mount-liveness-refresh
```

Expected log behavior:
- Silverfang should continue to count as tracked after the initial stale-component window.
- The previous `mount_tracked=0` shortly after a valid `tracked mount profile=ground` line should no longer happen while the same mount marker/root remains valid.
