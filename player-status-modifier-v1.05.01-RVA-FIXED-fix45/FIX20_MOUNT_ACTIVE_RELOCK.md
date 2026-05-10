# Fix 20: mount active relock

Base: `safe-rva-fixed-fix19-mount-stat-component-resolver`.

## Log finding

The fix19 test log proved registered-horse discovery:

```text
runtime: tracked mount profile=horse ...
```

But it did not show:

```text
runtime: locked mount health write ...
runtime: locked mount stamina ...
```

The tracked horse health/stamina entries did not reappear in the existing
write/delta hooks, so hook-only mount locking still had no runtime proof.

## Change

Once a mount has passed the existing full actor/stat resolver and supported
mount profile checks, the mount resolver loop now actively relocks the tracked
health/stamina current values to the configured `[Mount] LockValue`, clamped to
the stat maximum.

The active write is still guarded by:

- `[General] Enabled=1`
- `[Mount] Enabled=1`
- `LockHealth` / `LockStamina`
- a valid tracked mount snapshot
- supported mount health/stamina maxima
- safe SEH-protected stat reads/writes

## Expected proof

```text
dllmain: source build = safe-rva-fixed-fix20-mount-active-relock
runtime: tracked mount profile=horse ...
runtime: relocked mount health ...
runtime: relocked mount stamina ...
```
