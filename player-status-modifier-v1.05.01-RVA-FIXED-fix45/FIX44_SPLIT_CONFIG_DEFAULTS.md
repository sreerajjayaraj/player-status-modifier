# Fix44 split config defaults

Superseded by `FIX45_LAYERED_CONFIG_MERGE.md`, which adds actual multi-file layered config loading.

Fix44 changes the INI default model so omitted sections are neutral. This is required for real split presets.

Before this build, `ModConfig next{}` started with several active defaults:

- outgoing damage enabled at `2.0`
- item gain enabled at `2.0`
- health, stamina, and spirit modifiers enabled
- dragon village/flying hooks enabled

That meant a short `damage-only` INI could still install unrelated hooks unless it carried many neutral override sections. Fix44 makes those missing sections neutral in code.

## Presets

- `player-status-modifier.ini` is the normal full config and lists every supported option.
- `player-status-modifier.damage-only.ini` only contains `[General]`, `[OutgoingDamage]`, and `[IncomingDamage]`.
- `player-status-modifier.affinity-only.ini` only contains `[General]` and `[Affinity]`.
- `player-status-modifier.affinity-diagnostics.ini` only contains `[General]` and read-only affinity diagnostic toggles.

The `config-presets` folder contains drop-in copies where each preset is already named `player-status-modifier.ini`.

## Result

With fix44, a split preset only activates hooks for the sections it declares. For example, the damage-only preset no longer installs item, affinity, durability, mount, dragon, health, stamina, or spirit hooks.
