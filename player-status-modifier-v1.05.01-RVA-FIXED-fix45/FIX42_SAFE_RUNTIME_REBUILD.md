# Fix42 - safe runtime rebuild

Build id:

`safe-rva-fixed-fix42-safe-runtime-rebuild`

Purpose:

- Rebuild the stable fix38/fix39 runtime with an honest source-build id.
- Keep the working fix39 broad-friendly DMM JSON as the main relationship data
  patch.
- Supersede the unsafe external pet-hook experiment without carrying over its
  live `affinity-pet-*` hooks.

Runtime status:

- Health, stamina, spirit, incoming damage, outgoing damage, item gain,
  durability chance, mount relock, dragon options, and position toggles remain
  the same as the fix38 stable runtime.
- `UseStatWriteFallback=0` remains the default for both outgoing and incoming
  damage.
- The ASI does not install `affinity-vary-pet-reloc` or
  `affinity-vary-pet-rsrc`.

Affinity status:

- `Player_Status_Modifier_Affinity_100_BroadFriendly_DMM.json` remains the
  recommended DMM data patch.
- NPC greeting is expected to reach `+100`.
- Beggar coin donation is expected to cap at `+100`.
- NPC gift affinity may be affected only if it uses one of the broad friendly
  data rows; this is not yet confirmed.
- Petting affinity remains unresolved and should be treated as vanilla/gear
  based until a safe pet-specific path is found.

Packaging status:

- Main release artifacts should include the ASI, INI presets, SHA256 sums, and
  the broad-friendly DMM JSON only.
- Do not ship the failed pet-gear diagnostic JSON in the main release bundle.

Build dependency note:

- `PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR` can point CMake/SafetyHook at a
  local Zydis 4.1.0 source tree for offline builds.
- If that Zydis tree does not contain `dependencies/zycore`, also set
  `PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR` to a valid Zycore source tree.
