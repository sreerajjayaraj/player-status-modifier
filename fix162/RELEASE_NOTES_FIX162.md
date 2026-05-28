# Release Notes - fix162

fix162 is the accepted Crimson Desert 1.08 baseline for player Stamina and Spirit scaling.

## Confirmed Behavior

- Stamina consumption multiplier works before and after boss rematch.
- Stamina heal multiplier works before and after boss rematch.
- Spirit consumption multiplier works before and after boss rematch.
- Spirit heal multiplier works before and after boss rematch.
- Normal mount stamina lock works.
- Special mount stamina lock works.
- Mount health support remains enabled.

## Implementation Summary

- Adds a narrow post-rematch detached player stamina mirror acceptance path.
- Keeps rejected fix160-style broad stat-write argument scaling out of the accepted path.
- Leaves Freedom Flyer flight/glide behavior untouched.

## Packaging

Recommended DMM package contents:

- `player-status-modifier.asi`
- `player-status-modifier.ini`

Both files should be placed directly in the DMM `mods` folder.
