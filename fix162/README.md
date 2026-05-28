# Player Status Modifier for Crimson Desert

Community ASI mod for Crimson Desert that lets players tune player status, damage, item gain, durability, affinity data patches, and rideable mount stamina/health behavior through INI configuration files.

Current build: `1.08-fix162`

Build ID: `safe-rva-fixed-108-fix162-post-rematch-detached-mirror-scaling`

## Main Features

- Player health consumption/heal multipliers.
- Player stamina and spirit consumption/heal multipliers with player-only stat guards.
- Incoming and outgoing damage multipliers.
- Item gain multiplier.
- Durability consumption chance control.
- Rideable mount stamina and health lock support for current rideable-proxy mount layouts.
- Layered modular INI configuration.
- Optional affinity-related companion data patches.
- Optional position-control settings, only active when the matching hook is available.

## Latest Fixes

- Fixed post-boss-rematch player stamina mirror handling for Crimson Desert 1.08.
- Stamina consumption and heal multipliers now apply before and after boss rematch.
- Spirit consumption and heal multipliers now apply before and after boss rematch.
- The post-rematch detached stamina resource path is accepted only when it matches the validated player resource profile.
- Preserved normal mount and special mount stamina/health support.
- Bosses and large enemies are no longer treated as rideable mounts.
- Mount health lock now applies only to confirmed tracked rideable mount health entries.
- Rideable mount stamina relock remains active for current game mount layouts.
- Static `GetAsyncKeyState` import removed from the ASI.
- MSVC `std::thread` usage replaced with Win32 worker threads to reduce suspicious scan triggers.
- Empty PE Thread Storage Directory stripped from the release ASI after verifying the TLS callback table is empty.
- Windows version metadata added to the ASI.

## Installation With Definitive Mod Manager

1. Put `player-status-modifier.asi` and the desired INI files in the DMM `mods` folder.
2. Enable the ASI in DMM.
3. Use DMM to deploy into the game folder.
4. Avoid keeping backup ASI files inside the DMM `mods` folder or the game `bin64` folder.

The fix162 release package is prepared separately for NexusMods with ASI-only, INI-only, and full DMM upload archives.

## Modular Configuration

The default `player-status-modifier.ini` contains all options.

Additional layer files can be used for focused setups:

- `player-status-modifier.damage.ini`
- `player-status-modifier.mount.ini`
- `player-status-modifier.stamina-spirit.ini`
- `player-status-modifier.health.ini`
- `player-status-modifier.items.ini`
- `player-status-modifier.durability.ini`
- `player-status-modifier.resistance.ini`
- `player-status-modifier.affinity.ini`
- `player-status-modifier.affinity-diagnostics.ini`

Layer files are real configuration inputs, not placeholders. Users can combine them, for example damage-only, damage plus mount, or full player status plus mount plus affinity.

See `docs/CONFIG_LAYERS_README.md` for details.

## Build Notes

This project uses CMake and MSVC x64.

The release ASI is post-processed with:

`tools/strip-empty-tls-directory.js`

The script only strips the PE TLS directory if the callback table is empty. It refuses to modify a file with a non-empty TLS callback table.

## Release Hashes

See `SHA256SUMS.txt`.
