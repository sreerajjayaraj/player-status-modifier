# Fix 19: mount stat-component resolver

Base: `safe-rva-fixed-fix18-stamina-deadlock-guard`.

## Change

The stats hook already receives both the stat entry and its owning status/stat
component. Fix18 only used that component when it matched the tracked player,
so horse-sized non-player entries stayed diagnostic-only and never promoted to
the tracked mount.

Fix19 lets non-player stat components attempt a guarded mount resolve:

- the player must already be resolved
- the component must not be the player component
- the component must resolve into a full actor snapshot
- the observed stat entry must belong to that snapshot
- the snapshot must match the supported mount profile, including both health
  and stamina maxima

This keeps the old broad-health lock disabled while giving registered horses a
real discovery path when their stat component appears in the shared stats hook.

The background dragon pointer-chain resolver now respects the existing
`[General] StaleComponentMs` window before clearing a mount discovered by
another hook path. That prevents the dragon-only chain from immediately erasing
a horse mount, while still allowing stale mount state to expire.

## Expected proof

```text
dllmain: source build = safe-rva-fixed-fix19-mount-stat-component-resolver
runtime: tracked mount profile=horse ...
runtime: locked mount health write ...
runtime: locked mount stamina ...
```
