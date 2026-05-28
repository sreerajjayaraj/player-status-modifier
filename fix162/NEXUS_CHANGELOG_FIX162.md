# Player Status Modifier fix162 - NexusMods Changelog

Game target: Crimson Desert 1.08

Build ID: `safe-rva-fixed-108-fix162-post-rematch-detached-mirror-scaling`

This build is the accepted 1.08 resource-scaling baseline after a long stamina/spirit regression pass. The main goal was to make Stamina and Spirit multipliers work both before and after boss rematch without breaking mounts, special mounts, Freedom Flyer compatibility, or normal player actions.

## Fixed in fix162

- Fixed the post-boss-rematch Stamina path by accepting the detached post-rematch player stamina mirror only when it matches the validated player resource profile.
- Stamina ConsumptionMultiplier now works before and after boss rematch.
- Stamina HealMultiplier now works before and after boss rematch.
- Spirit ConsumptionMultiplier now works before and after boss rematch.
- Spirit HealMultiplier now works before and after boss rematch.
- Preserved normal mount stamina lock.
- Preserved special mount stamina lock.
- Preserved normal and special mount health lock support.
- Kept Freedom Flyer v1.17.2 compatibility intact.

## Major Test History

- Early 1.08 testing showed that Stamina and Spirit behaved differently before and after boss rematch. The desired target was the stable post-rematch behavior from the earlier fix138 line.
- Spirit was traced first. Several builds proved that display-only or forced-refresh approaches were not acceptable because they could create drain/regen loops, delayed HUD updates, or Force Palm crashes.
- Focus Mode testing confirmed the correct Spirit behavior: Focus Mode should recover Spirit, not consume it, and Spirit HealMultiplier should amplify real recovery only.
- Force Palm, Turning Slash, elemental imbue Turning Slash, and Focus Mode were repeatedly tested before and after boss rematch to separate real Spirit consumption from HUD/cache artifacts.
- Stamina testing confirmed that sprint, dodge, jump, heavy attacks, and gliding/flying can use different write paths. Some earlier builds fixed Spirit but left Stamina scaling broken after rematch.
- fix160 tested broad post-rematch stat-write argument scaling. It was rejected because it caused sprint/flight instant-drain behavior and made Stamina unusable in some cases.
- fix161 narrowed that rejected path so exact tracked player entries were not scaled through the dangerous argument-scaling route.
- fix162 added the missing post-rematch detached player Stamina mirror path while keeping classification narrow enough to avoid enemy, boss, mount, or special-mount misclassification.
- Final live tests confirmed that Stamina and Spirit consumption/heal multipliers work before and after boss rematch.
- Normal mount and special mount stamina behavior were checked and remained working.

## What This Build Does Not Change

- It does not rewrite Freedom Flyer flight movement.
- It does not re-enable Enhanced Flight or other flight/glide/vertical movement ASI conflicts.
- It does not use the rejected broad fix160 stat-write scaling path.
- It does not intentionally change field JSON mods, DMM behavior, CGM exports, or game files.

## Known Follow-Up

- Freedom Flyer wing/glide flight can still show vanilla-looking visible stamina drain even when the tracked backend Stamina value is scaled. This appears to be a separate flight HUD/cache/timer path and should be diagnosed from fix162 without changing the accepted Stamina/Spirit baseline.

## Installation

Use Definitive Mod Manager.

Place `player-status-modifier.asi` and `player-status-modifier.ini` directly inside the DMM `mods` folder, enable the ASI, then deploy. Do not place these files in a nested subfolder.

For live tuning, remember that the game uses the deployed `bin64` INI after DMM deployment.
