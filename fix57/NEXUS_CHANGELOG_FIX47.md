# Nexus changelog - fix47

- Compatibility update for the May 11, 2026 game update / `CrimsonDesert.exe` file version `1.0.0.1235`.
- Updated scanner targets for the new executable layout.
- Restored hook resolution for position control, dragon limit options, affinity friendly scaling, pet affinity diagnostics, and affinity item-event diagnostics.
- Kept the core fix46 behavior: layered INI loading, modular DMM configs, damage safeguards, mount tracking, stamina/spirit handling, durability control, and item gain scaling.
- Kept old v1.05.01 hook candidates as safe fallbacks where practical.
- Added a verified fix47 build id: `safe-rva-fixed-fix47-v1001235-compat`.

Note: after a game update, remove/replace older ASI files from DMM and make sure only one `player-status-modifier.asi` is active.
