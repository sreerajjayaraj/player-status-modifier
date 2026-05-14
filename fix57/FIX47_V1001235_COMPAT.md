# Fix47 - CrimsonDesert.exe 1.0.0.1235 compatibility

Build id: `safe-rva-fixed-fix47-v1001235-compat`

Target game executable verified during this pass:

- `CrimsonDesert.exe` file version: `1.0.0.1235`
- SHA256: `50F9D4E77E3E99DBBEE4880666335B0EA92E87347BD1FDC7547BA7707E041ED2`

## What changed

- Updated the optional position-height scanner from a single v1.05.01 RVA to a pattern-first resolver with known RVA fallback.
- Updated dragon village summon, forced-flying restriction, and roof restriction resolvers for the new executable layout.
- Updated affinity friendly callsites for the new executable so the friendly copy-on-write scaler can install again when legacy affinity prepare/current byte checks are rejected.
- Updated affinity pet diagnostic callsites for the new executable.
- Updated gift/take affinity item event diagnostic probe callsites for the new executable.
- Kept old v1.05.01 RVA candidates as fallbacks where safe.

## Static verification

The following updated sites were statically verified against `CrimsonDesert.exe` 1.0.0.1235:

- Position height: `0x0384782C`
- Dragon village: `0x01CC04A9`
- Dragon flying restriction: `0x01E4746B`
- Dragon roof restriction: `0x002E42CB`
- Affinity vary friendly: `0x0F48EFF4`
- Affinity vary friendly with logout: `0x0132DDFB`
- Affinity pet diagnostic reloc: `0x0130FFF5`
- Affinity pet diagnostic rsrc: `0x0C563647`
- Affinity item give event probe: `0x0138B64B`
- Affinity item take event probe: `0x0138B742`

## Notes

- Core hooks for player tracking, mount tracking, damage, item gain, durability, stamina, spirit, stats, and stat-write were already still uniquely matched on the updated executable.
- Legacy affinity prepare/current still rejects on byte checks for this game update; the compatible path is the friendly copy-on-write scaler pair.
- Runtime gameplay testing is still required after DMM deploys the new ASI, because static verification cannot prove every in-game branch is exercised.
