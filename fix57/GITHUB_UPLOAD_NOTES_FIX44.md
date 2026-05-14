# GitHub upload notes - fix44

Build id: `safe-rva-fixed-fix44-split-config-defaults`

Fix44 is a configuration cleanup and safety build. The important behavior change is that missing INI sections now default to neutral, so specialized configs can be genuinely isolated.

## Included configs

- `player-status-modifier.ini`: full normal config with every option present.
- `player-status-modifier.damage-only.ini`: damage control only.
- `player-status-modifier.affinity-only.ini`: affinity control only.
- `player-status-modifier.affinity-diagnostics.ini`: affinity diagnostics only.
- `config-presets/full/player-status-modifier.ini`: drop-in full preset.
- `config-presets/damage-only/player-status-modifier.ini`: drop-in damage-only preset.
- `config-presets/affinity-only/player-status-modifier.ini`: drop-in affinity-only preset.
- `config-presets/affinity-diagnostics/player-status-modifier.ini`: drop-in diagnostics preset.

`player-status-modifier.default.ini` and `player-status-modifier.safe-tested.ini` were removed because they duplicated the normal config and made preset selection confusing.

## Notes

- The ASI still reads `player-status-modifier.ini` at runtime.
- For DMM/manual preset use, choose one folder under `config-presets` and deploy that folder's `player-status-modifier.ini`.
- Affinity petting remains unresolved by the ASI route; the diagnostics preset is for further pet/gift route research.
