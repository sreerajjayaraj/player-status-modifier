# Fix 52: rideable proxy mount profile

Base: `safe-rva-fixed-fix51-special-mount-research`.

## Why

Testing with a summoned Lion and Royler, the legendary white horse, showed the
same failure pattern:

- the mount was first tracked as a safe ground profile with `health_max=300000`
  and `stamina_max=30000`
- immediately after riding, the same non-player marker/root/stat entries
  reported `health_max=1650000` and `stamina_max=170000`
- the old guard rejected that as an unsupported profile, cleared mount tracking,
  and mount stamina depletion continued

## Change

Added a guarded `rideable-proxy` profile:

- only accepted after a mount has already been identified by the old safe ground
  or dragon profile
- requires the same non-player marker, root, health entry, and stamina entry
- rejects the player marker/root
- accepts player-scale rideable proxy maxima in a narrow range
- keeps ordinary `70000` animal health candidates diagnostic-only unless they
  are tied to a resolved ridden mount context

This lets the active relock loop continue using the same ridden mount entries
after the game swaps them into the rideable proxy stat profile.

## Expected proof

```text
dllmain: source build = safe-rva-fixed-fix52-rideable-proxy-mount
runtime: accepted rideable proxy mount profile ...
runtime: relocked mount stamina ...
```
