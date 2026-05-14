# GitHub upload notes - fix46

Build id: `safe-rva-fixed-fix46-layered-config-precedence`

Fix46 is the layered config build with corrected load precedence.

## Load order

1. `player-status-modifier.default.ini`
2. `player-status-modifier.ini`
3. Any other sibling `player-status-modifier.*.ini` files, alphabetically

## Why this matters

The full default config is retained for users who want all options. It now acts as the base layer instead of overriding a user-edited `player-status-modifier.ini`.

Module files still combine freely:

- damage + affinity
- damage + mount
- damage + mount + stamina/spirit + affinity

Missing sections remain neutral.
