# Fix 53: mount spirit-stamina lock

Base: `safe-rva-fixed-fix52-rideable-proxy-mount`.

## Why

Fix52 kept Lion/Royler rideable-proxy mount tracking alive and relocked mount
health, but visible mount stamina still depleted. The logs showed the mount also
has a `health + 0x510` type-18 resource entry, while the existing mount lock
only actively relocked `health + 0x5A0` stamina.

## Change

- When `[Mount] LockStamina=1`, active relock now also locks the tracked mount
  `spirit_entry` as `spirit-stamina`.
- The spirit-delta path now treats tracked `MountSpirit` as mount stamina and
  cancels/restores negative deltas when mount stamina locking is enabled.

## Expected proof

```text
dllmain: source build = safe-rva-fixed-fix53-mount-spirit-stamina-lock
runtime: relocked mount spirit-stamina ...
runtime: locked mount spirit-stamina delta ...
```
