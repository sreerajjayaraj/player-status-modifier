# Fix35 - affinity data patch companion

Build id:

`safe-rva-fixed-fix35-affinity-data-patch-companion`

Purpose:

- Keep the stable fix34 ASI behavior and diagnostics.
- Add a DMM data patch for the affinity values that proved to be data-driven
  rather than controlled by the current runtime affinity hook.

Findings from fix34:

- The ASI loaded correctly and the item-gain hook fired during affinity
  gameplay, but the visible NPC affinity gain stayed at the vanilla `+5`.
- The item-gain target values were large reward counters, not the relationship
  value shown in the UI.
- The known 1.05.01 data route is `DropSet_Friendly_Talk` in
  `gamedata/dropsetinfo.pabgb`.
- Pet abyss gear friendliness bonuses live in `gamedata/iteminfo.pabgb` and
  are capped by the game at `+10`.

Included companion patch:

`release-artifacts/Player_Status_Modifier_Affinity_3x_DMM.json`

Expected behavior with the companion DMM patch enabled:

- NPC greeting friendly talk gain: `+5 -> +15`
- Superseded by fix36 gameplay: petting base gain remains vanilla `+5`; the
  observed pet abyss gear result is `+5` base plus the separate `+10` gear
  bonus. The fix35 companion patch did not affect petting base gain.

Disable other trust/friendly data patches while testing this one, especially
older `friendly_gain_x20` or Trusty trust presets, so the result is unambiguous.
