# GitHub upload notes - fix35

Build id:

`safe-rva-fixed-fix35-affinity-data-patch-companion`

Purpose:

- Stable continuation of fix34.
- Adds a DMM companion patch for data-driven affinity gain:
  `release-artifacts/Player_Status_Modifier_Affinity_3x_DMM.json`.

Why:

- Fix34 confirmed the ASI ran and the item-gain instruction was not the visible
  relationship gain.
- The visible NPC gain stayed at the vanilla `+5`.
- Existing 1.05.01 DMM/Trusty research identifies `DropSet_Friendly_Talk` as the
  data path for the default trust gain.

Expected test result:

- NPC greeting friendly gain should become `+15`.
- Superseded by fix36 gameplay: petting base gain stays vanilla `+5`; the pet
  abyss gear bonus is a separate `+10` effect, so the observed total can remain
  `+15` without the DMM JSON affecting petting base.

Testing note:

Disable overlapping trust/friendly patches while testing this companion JSON.
