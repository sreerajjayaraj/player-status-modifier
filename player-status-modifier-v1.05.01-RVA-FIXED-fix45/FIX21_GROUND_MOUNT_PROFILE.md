# Fix 21: ground mount profile

Base: `safe-rva-fixed-fix20-mount-active-relock`.

## Why

Gameplay uses more than one registered mount type, including horses and a wolf.
Fix19 proved mount discovery, but the runtime/profile wording and thresholds
were still horse-oriented.

## Change

The non-dragon mount profile is now treated as a guarded `ground` profile rather
than a horse-only profile. The minimum resolved profile was lowered to cover
smaller ground mounts while preserving the important safety boundary:

- broad health/damage-root callbacks remain dragon-only
- ground mounts are accepted only through resolved mount-owned stat/stamina
  context or explicit mount marker chains
- active relock still writes only after a full resolved mount snapshot passes
  profile checks

The tracked-mount log now includes `health_max` and `stamina_max`, so the next
test can show the actual maxima for each horse/wolf mount observed.

## Expected proof

```text
dllmain: source build = safe-rva-fixed-fix21-ground-mount-profile
runtime: tracked mount profile=ground ... health_max=... stamina_max=...
runtime: relocked mount health ...
runtime: relocked mount stamina ...
```
