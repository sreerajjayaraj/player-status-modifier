# Config layers

The ASI loads configuration from files next to the ASI.

Loaded files, in order:

1. `player-status-modifier.default.ini`
2. `player-status-modifier.ini`
3. Every other sibling `player-status-modifier.*.ini`, alphabetically

To combine options, place the selected layer files in the same folder as the ASI:

- `player-status-modifier.damage.ini`
- `player-status-modifier.affinity.ini`
- `player-status-modifier.mount.ini`
- `player-status-modifier.stamina-spirit.ini`
- `player-status-modifier.health.ini`
- `player-status-modifier.items.ini`
- `player-status-modifier.durability.ini`
- `player-status-modifier.resistance.ini`
- `player-status-modifier.affinity-diagnostics.ini`

The all-options config is still available as `player-status-modifier.ini` and `player-status-modifier.default.ini`. The default file acts as a base layer; the primary INI and module files can override it.

If an INI stays inside a subfolder such as `module-configs`, it is just a template and will not be read until copied beside the ASI.

Module files intentionally avoid unrelated off-switches, so selected layers can stack without disabling each other.
