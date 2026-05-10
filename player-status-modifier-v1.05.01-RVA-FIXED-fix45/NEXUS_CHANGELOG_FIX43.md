# Nexus changelog - fix43 affinity diagnostics gated

- Rebuilt from the stable fix42 runtime.
- Added disabled-by-default `[Affinity] GiftDiagnostics`.
- Added disabled-by-default `[Affinity] PetDiagnostics`.
- Added a neutral `player-status-modifier.affinity-diagnostics.ini` preset.
- Pet diagnostic hooks are read-only and do not modify affinity values.
- Gift event probes now allow more log samples when diagnostics are enabled.
- Normal gameplay presets keep diagnostics disabled.
- Kept the broad-friendly JSON for NPC greeting and donation affinity.
- Known limitation: petting affinity is still not modified.
- Known limitation: NPC gift affinity is still not modified.
