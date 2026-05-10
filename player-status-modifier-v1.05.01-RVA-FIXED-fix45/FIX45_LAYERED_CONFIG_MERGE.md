# Fix45 layered config merge

Fix45 makes the INI split real. The ASI no longer treats `player-status-modifier.ini` as the only runtime config file.

At startup it now reads:

1. `player-status-modifier.ini`, if present.
2. Every sibling file matching `player-status-modifier.*.ini`.

Each layer only applies the keys it actually contains. Missing sections do not reset previous layers and do not enable hidden defaults.

## Examples

- Damage only: deploy `player-status-modifier.damage.ini`.
- Affinity plus damage: deploy `player-status-modifier.affinity.ini` and `player-status-modifier.damage.ini`.
- Damage plus mount: deploy `player-status-modifier.damage.ini` and `player-status-modifier.mount.ini`.
- Damage plus mount plus stamina/spirit plus affinity: deploy those four layer files together.
- All-options/default config: deploy `player-status-modifier.ini` or `player-status-modifier.default.ini`.

The log now prints the primary config path, layer count, and every loaded config layer.

## Important behavior

- Layer files must be next to the ASI in the game `bin64` folder after DMM/manual deployment.
- If the default all-options config is present with layer files, it also participates in the merge.
- Later layer files can override earlier values when they set the same key.
