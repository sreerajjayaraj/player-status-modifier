# Fix56 Mounted Spirit-Stamina Hardlock

## Problem

Fix55 correctly routed horse stamina drain through the spirit handler, but the log showed the normal spirit consumption multiplier was still applied:

```text
old=-11400000 final=-1140000
```

That is correct for normal player spirit consumption, but wrong for `[Mount] LockStamina=1`. While mounted, the game uses the player/rider spirit entry through the stamina hook as a rideable stamina action cost, so any negative final delta still appears as mount stamina depletion.

## Change

- In `StaminaAb00Callback`, when a mount is tracked and `[Mount] LockStamina=1`, negative type-18 deltas against the tracked player spirit entry are hard-locked to `0`.
- Unmounted player spirit behavior still uses the regular spirit multiplier path.
- Positive spirit deltas still use the existing spirit heal multiplier path.

## Expected Log Evidence

Horse/Lion/Royler mounted stamina use should now show:

```text
hooks: stamina-ab00 mounted spirit-stamina locked ... final-delta=0 ...
```

