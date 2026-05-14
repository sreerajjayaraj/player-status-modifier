# Fix48 mount stat-write and legacy INI compatibility

- Restores backward compatibility for older `player-status-modifier.ini` files that still use
  `[Health]`, `[Stamina]`, and `[Spirit]` multipliers without explicit `Enabled=1`.
- Treats non-neutral legacy stat multipliers as an implicit enable signal during config load.
- Forces the legacy stat-write hook to install when mount health lock is enabled.
- Fixes the `fix47` runtime state where mount tracking was active but `stat-write=0`, so ground mount
  health could be observed without being relocked.
- Keeps the `fix47` game-update compatibility work for `CrimsonDesert.exe` version `1.0.0.1235`.
