# Fix57 Mounted Type18 Stamina Hardlock

## Problem

Fix56 correctly locked negative rider-spirit deltas, but the horse-only log still showed additional type-18 entries in the `stamina-ab00` hook while mounted:

```text
hooks: stamina-ab00 raw skipped spirit type=18 entry=... rbx=-100000 ...
```

Those entries were not the tracked player spirit or tracked mount spirit pointer, so fix56 skipped them. Because they occur in the same mounted stamina hook path while a rideable mount is tracked, they are likely auxiliary mount stamina pools used by the horse UI/action gate.

## Change

- While a mount is tracked and `[Mount] LockStamina=1`, all negative type-18 deltas in `stamina-ab00` are hard-locked to `0`.
- Positive type-18 deltas still flow through the existing spirit recovery path.
- Unmounted player spirit behavior is unchanged.
- Logging now includes `matched=1` for player spirit, `matched=2` for tracked mount spirit, and `matched=0` for auxiliary mounted type-18 entries.

## Expected Log Evidence

The previously skipped auxiliary entries should now show:

```text
hooks: stamina-ab00 mounted spirit-stamina locked ... matched=0 ...
```

