# Nexus changelog - fix44

- Reworked config defaults so missing INI sections are neutral instead of silently enabling unrelated systems.
- Added real split presets:
  - full normal config with every option
  - damage-only config
  - affinity-only config
  - affinity diagnostics config
- Added `config-presets` folders with each preset already named `player-status-modifier.ini` for drop-in use.
- Removed duplicate `default` and `safe-tested` INI presets from the release to reduce confusion.
- Damage-only preset now avoids item, affinity, durability, mount, dragon, health, stamina, and spirit hooks unless explicitly added.
- No new pet affinity hook is enabled in this build; petting affinity still needs separate route work.
