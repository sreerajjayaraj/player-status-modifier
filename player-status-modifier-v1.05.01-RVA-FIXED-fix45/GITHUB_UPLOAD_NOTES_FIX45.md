# GitHub upload notes - fix45

Build id: `safe-rva-fixed-fix45-layered-config-merge`

Fix45 changes configuration from alternate templates to true layer files. The ASI reads `player-status-modifier.ini` plus every sibling `player-status-modifier.*.ini`, then merges only the keys present in each file.

## Runtime config files

- `player-status-modifier.ini`: normal all-options config.
- `player-status-modifier.default.ini`: all-options config that also works as a layer if used without the primary INI.
- `player-status-modifier.damage.ini`: damage layer.
- `player-status-modifier.affinity.ini`: affinity layer.
- `player-status-modifier.mount.ini`: mount layer.
- `player-status-modifier.stamina-spirit.ini`: stamina and spirit layer.
- `player-status-modifier.health.ini`: health layer.
- `player-status-modifier.items.ini`: item gain layer.
- `player-status-modifier.durability.ini`: durability layer.
- `player-status-modifier.resistance.ini`: resistance layer.
- `player-status-modifier.affinity-diagnostics.ini`: read-only affinity diagnostics layer.

## Usage

Place the ASI and whichever INI layers you want next to each other in `bin64`.

Examples:

- Affinity plus damage: use `player-status-modifier.affinity.ini` and `player-status-modifier.damage.ini`.
- Damage plus mount: use `player-status-modifier.damage.ini` and `player-status-modifier.mount.ini`.
- Damage plus mount plus stamina/spirit plus affinity: use all four relevant layer files.
- Full config: use `player-status-modifier.ini` or `player-status-modifier.default.ini`.

Missing sections are neutral, so a layer does not silently enable unrelated systems.
