# Fix54 Mount Spirit Hook Route

## Problem

Gameplay logs for Lion and Royler still showed mount stamina depletion. The deployed game copy was still fix52, so fix53 was not actually being tested. While reviewing the fix53 source, there was also a real routing issue: the spirit delta hook only allowed the tracked player spirit entry to continue into `TryAdjustSpiritDelta`.

Some newer rideable animals expose their rider-facing stamina drain through the mount `spirit_entry`. Fix53 added mount spirit-stamina locking in the runtime logic, but the hook returned before that logic could run.

## Change

- Updated `SpiritDeltaCallback` to accept either the tracked player spirit entry or the tracked mount spirit entry.
- Added low-volume hook logging that labels accepted spirit delta callbacks as `matched=player` or `matched=mount`.
- Kept existing player spirit behavior unchanged.
- Build id is now `safe-rva-fixed-fix54-mount-spirit-hook-route`.

## Expected Log Evidence

When a rideable mount drains through the spirit slot, the log should show:

```text
hooks: spirit-delta callback ... matched=mount ...
runtime: locked mount spirit-stamina delta ...
```

The active relock path may also show:

```text
runtime: relocked mount spirit-stamina ...
```

