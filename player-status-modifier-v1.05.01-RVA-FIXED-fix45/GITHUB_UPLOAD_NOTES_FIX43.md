# GitHub upload notes - fix43 affinity diagnostics gated

Build id:

`safe-rva-fixed-fix43-affinity-diagnostics-gated`

Main changes:

- Rebuilt from fix42 with gated affinity diagnostics.
- Default behavior remains stable: gift/pet diagnostics are disabled unless
  explicitly enabled in the INI.
- Added `[Affinity] GiftDiagnostics`.
- Added `[Affinity] PetDiagnostics`.
- Added `player-status-modifier.affinity-diagnostics.ini` for neutral logging
  tests.
- Pet diagnostic hooks are read-only and do not write affinity data.
- Gift event probes use a larger sample budget when gift diagnostics are
  enabled.
- Keeps local Zydis/Zycore build options from fix42.

Recommended upload folder:

`player-status-modifier-v1.05.01-RVA-FIXED-fix43-affinity-diagnostics-gated-github`

Known limitations:

- Petting is not modified yet.
- NPC gift affinity is not modified yet.
- The broad-friendly JSON still covers greeting and donation paths.
