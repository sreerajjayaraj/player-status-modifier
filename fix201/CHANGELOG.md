# Changelog

## 1.09-fix201-itemgain-shadow-guard

- Preserved the fix200 playable-character handling for Kliff, Damiane, and Oongka.
- Preserved health, stamina, and spirit multiplier behavior from the verified fix200 line.
- Added a primary-call guard for the item gain hook.
- Skips duplicate or shadow item-gain calls that do not look like real inventory gain operations.
- Reduced risk of item-gain scaling affecting transition or interaction counters.
- Confirmed DMM and live `bin64` deployment hashes matched before validation.
- Confirmed no previously reported gameplay issue reproduced in the fix201 log review.

## 1.09-fix200-kliff-owner-damage

- Fixed Kliff incoming damage/fall damage regression after owner tracking changed.
- Removed loose player-shaped stamina promotion from the mount-stamina path.
- Kept playable-character resource owner handling stable for Kliff, Damiane, and Oongka.

## 1.09-fix199-health-stamina-test

- Test-only line for incoming damage and playable-character health/stamina verification.
- Superseded by fix200 and fix201.

