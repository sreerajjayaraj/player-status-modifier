# GitHub upload notes - fix42 safe runtime rebuild

Build id:

`safe-rva-fixed-fix42-safe-runtime-rebuild`

Main changes:

- Rebuilt the stable runtime with a new build id so logs no longer identify the
  package as fix38.
- Keeps the fix38 damage-only safeguards:
  - `UseStatWriteFallback=0` by default.
  - Damage-only preset available.
  - Health, stamina, and spirit can be disabled independently.
- Keeps the fix39 broad-friendly relationship JSON:
  - NPC greeting target `+100`.
  - Donation target `+100`.
  - Additional positive friendly rows patched to `100` for diagnostics.
- Excludes the unsafe external `affinity-pet-*` live hooks.
- Excludes the failed pet-gear diagnostic JSON from the main release artifacts.

Known status:

- Petting affinity remains unresolved and still follows vanilla / gear behavior.
- NPC gift affinity is still not confirmed as multiplied.
- If a user only wants outgoing damage reduction, recommend
  `player-status-modifier.damage-only.ini`.
- Source builds can use local Zydis/Zycore paths through
  `PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR` and
  `PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR`.

Recommended upload folder:

`player-status-modifier-v1.05.01-RVA-FIXED-fix42-safe-runtime-rebuild-github`
