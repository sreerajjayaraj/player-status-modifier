# Fix 17: stamina/mount identity guard for Crimson Desert 1.05.01

Base: uploaded `player-status-modifier-v1.05.01-RVA-FIXED` source archive.

## Why this patch exists

The safe-source test log showed the stamina shared hook firing, but every useful callback was skipped because the
code still treated stat type `17` as stamina:

```text
hooks: stamina-ab00 raw callback ... rdi=0x...0013
hooks: stamina-ab00 raw skipped type=19 ...
```

In the same log, the observed 1.05.01 player stat layout was:

```text
health: type 0   at health + 0x000
spirit: type 18  at health + 0x510
stamina-like entry: type 19 at health + 0x5A0
```

So this patch switches the preferred 1.05.01 stamina id/offset to `19` / `+0x5A0`, while keeping the older
`17` / `+0x480` as a guarded fallback.

## Safety choices

- Existing working health, incoming damage, outgoing damage, item gain, spirit, durability, and affinity-crash guard logic is not intentionally changed.
- Type 18 remains spirit and is not treated as stamina.
- Type 19 is accepted as the primary 1.05.01 stamina id.
- Legacy type 17 remains accepted only as a fallback for older layouts.
- The ASI now logs its source build id and stamina constants at startup so future logs can be matched to the exact source line.
- No compiled ASI, loader DLL, or game executable is included.
