# Nexus changelog - fix42 safe runtime rebuild

- Rebuilt the stable fix38/fix39 runtime with a new fix42 build id.
- Excluded the unsafe external fix41 pet-affinity hook experiment that could
  crash on startup/load.
- Kept outgoing and incoming damage safeguards from fix38:
  `UseStatWriteFallback=0` by default.
- Added/retained `player-status-modifier.damage-only.ini` for users who only
  want outgoing/incoming damage options.
- Kept health, stamina, and spirit modifiers independently toggleable.
- Included the broad-friendly DMM JSON patch for relationship testing.
- NPC greeting affinity can be set to `+100` through the JSON patch.
- Beggar coin donation can reach the `+100` cap through the JSON patch.
- Removed the failed pet-gear diagnostic JSON from the main release artifacts.
- Known limitation: petting affinity is still vanilla / gear based.
- Known limitation: NPC gift affinity is still not confirmed as multiplied.
