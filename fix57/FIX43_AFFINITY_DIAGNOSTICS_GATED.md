# Fix43 - gated affinity diagnostics

Build id:

`safe-rva-fixed-fix43-affinity-diagnostics-gated`

Purpose:

- Keep the stable fix42 runtime behavior by default.
- Add explicit, disabled-by-default diagnostics for the unresolved affinity
  paths: NPC gifts and petting / animal affinity.
- Avoid the unsafe fix41 behavior by never modifying pet diagnostic arguments.

Runtime changes:

- Added `[Affinity] GiftDiagnostics=0`.
  - When enabled, gift event probes keep more samples in the log.
  - The probe remains read-only.
- Added `[Affinity] PetDiagnostics=0`.
  - When enabled, two candidate pet/animal friendly callsites are hooked for
    read-only logging.
  - The callback logs registers, stack candidates, `r9` struct fields, and
    `r13+0x38C..0x3A0` candidates.
  - The callback does not modify `r9`, stack memory, or game affinity values.
- Added `player-status-modifier.affinity-diagnostics.ini`, a neutral preset
  that disables damage/stat/item/durability changes and enables only read-only
  affinity diagnostics.

Known status:

- NPC greeting and beggar donation remain covered by the broad-friendly JSON.
- Petting affinity remains unresolved; fix43 is a logging build to identify the
  real pet delta path.
- NPC gift affinity remains unresolved; fix43 should provide clearer gift-event
  traces during real gift actions.

Testing guidance:

- Normal gameplay: use `player-status-modifier.ini`, `safe-tested`, or
  `damage-only` presets with `GiftDiagnostics=0` and `PetDiagnostics=0`.
- Diagnostic pass: use `player-status-modifier.affinity-diagnostics.ini`, then
  perform one NPC gift and one petting action and share the log.
