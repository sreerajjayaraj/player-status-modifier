# GitHub upload notes - fix49

- Source build id: `safe-rva-fixed-fix49-v1001245-compat`
- Base game target: `CrimsonDesert.exe` version `1.0.0.1245`
- Primary compatibility fix:
  refreshed the affinity-friendly and pet resource diagnostic RVAs moved by the latest game update.
- Preserved fix48 behavior:
  mount lock still installs the stat-write hook, and legacy stat multiplier INI files are still
  interpreted correctly.
- Recommended packaging:
  include the rebuilt `player-status-modifier.asi`, refreshed `SHA256SUMS.txt`, default/full INI,
  split module INIs, optional JSON patches, and DMM module zip.
