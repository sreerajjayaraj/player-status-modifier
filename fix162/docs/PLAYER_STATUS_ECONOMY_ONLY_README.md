# Player Status Economy Only

This parallel ASI intentionally affects only:

- Incoming damage taken by the player character.
- Positive item/currency gain amounts seen by the item-gain hook.

The ASI is named `player-status-economy.asi` and reads only `player-status-economy.ini`.
It writes `player-status-economy.log`.

This build clamps unrelated options off at compile time, so old full-mod config keys
for health, stamina, spirit, mount, affinity, durability, resistance, dragon limits,
position control, and outgoing damage are ignored by this ASI.

Do not enable this together with the full `player-status-modifier.asi`, because both
mods patch the same damage and item-gain functions.

