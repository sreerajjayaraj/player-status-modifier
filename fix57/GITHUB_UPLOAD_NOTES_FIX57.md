# GitHub Upload Notes - Fix57

## Confirmed Build

- Build id: `safe-rva-fixed-fix57-mounted-type18-stamina-hardlock`
- Confirmed gameplay result: mount stamina did not drain for normal and special mounts.
- Primary binary: `player-status-modifier.asi`

## Packaging Notes

- Keep backup or disabled ASI files outside the Definitive Mod Manager `mods` folder and outside the game `bin64` folder.
- DMM-compatible install layout is a `bin64` folder containing the ASI and selected INI files.
- The ASI reads INI files placed beside it in this order:
  1. `player-status-modifier.default.ini`
  2. `player-status-modifier.ini`
  3. Other `player-status-modifier.*.ini` files alphabetically
- Module INIs are intended to stack. Users can combine files such as damage plus mount, mount plus stamina/spirit, or all options together.

## Nexus Summary

Fix57 resolves the mounted stamina drain path observed on normal horse and special rideable mounts by locking negative type-18 mounted stamina deltas while `[Mount] LockStamina=1`.

