# Fix 24 Cached Ground Mount Liveness

Base: fix23 `safe-rva-fixed-fix23-ground-mount-liveness-refresh`.

Reason for this patch:
- The White Bear gameplay log showed the mod loaded fix23 and detected the mount:

```text
runtime: tracked mount profile=ground ... health_max=300000 ... stamina_max=30000
```

- There were still no `relocked mount` lines, and the first later ground-range health write reported `mount_tracked=0`.
- Because `StaleComponentMs=60000` and the first `mount_tracked=0` appeared about 28 seconds after tracking, the tracked mount was likely being cleared by the immediate active re-resolve path, not by the stale timer.
- The White Bear marker chain can apparently become temporarily unavailable right after the stat component identifies the mount.

Changes:
- If marker re-resolution fails after a valid ground-mount stat-component track, keep the cached mount alive as long as the cached health/stamina entries still validate:
  - supported ground/dragon max range,
  - expected `health + 0x5A0` or legacy `health + 0x480` layout,
  - valid stat ids and current/max values.
- Refresh `g_mount_last_seen_tick` when this cached validation succeeds.
- Add a bounded diagnostic line:

```text
runtime: kept tracked mount alive reason=cached-marker-unavailable ...
```

Expected new startup line:

```text
dllmain: source build = safe-rva-fixed-fix24-cached-ground-mount-liveness
```

Expected log behavior:
- After White Bear is tracked, the mod should not immediately lose `g_mount_resolve` just because the marker chain cannot be re-read.
- If the mount takes damage through the tracked `300000/30000` stat pair, mount health/stamina lock should now have a live tracked mount to act on.
- Separate outgoing-damage targets such as `max=450000` remain untrusted unless they resolve through mount context.
