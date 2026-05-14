# Fix55 Stamina AB00 Spirit Route

## Problem

Fix54 was loaded correctly, but Lion/Royler stamina still depleted. The log showed the active drain arriving through the `stamina-ab00` hook as a type `18` stat entry:

```text
hooks: stamina-ab00 raw callback ... rax=<player spirit> rbx=-11400000 ... rdi=0x12
hooks: stamina-ab00 raw skipped non-stamina type=18 ...
```

The tracked mount stamina and mount spirit-stamina entries stayed full, so the remaining depletion path was not the tracked mount stat entries. While mounted, the game is using a spirit-type entry through the stamina delta call path.

## Change

- `StaminaAb00Callback` now routes type `18` entries into `TryAdjustSpiritDelta` instead of skipping them as non-stamina.
- This allows the existing spirit consumption rules to zero or scale the delta even when the game uses the stamina hook for a spirit-backed rideable stamina cost.
- Added diagnostic logging:

```text
hooks: stamina-ab00 spirit adjusted ...
```

## Expected Log Evidence

For Lion/Royler stamina use, the log should show:

```text
hooks: stamina-ab00 spirit adjusted entry=... old-delta=-... final-delta=0 ...
```

