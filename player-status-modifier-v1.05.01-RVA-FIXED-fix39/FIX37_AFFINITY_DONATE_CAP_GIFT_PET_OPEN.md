# Fix37 - affinity donation cap and open gift/pet paths

Build id:

`safe-rva-fixed-fix37-affinity-donate-cap-gift-pet-open`

Purpose:

- Keep the stable fix36 ASI behavior and DMM companion patch workflow.
- Normalize donation affinity to the observed game cap: `+50 -> +100`.
- Keep the remaining unresolved affinity paths explicit: NPC gift and petting
  base gain are still not affected by the ASI or companion JSON.

Confirmed from gameplay:

- NPC greeting with the companion DMM JSON enabled: `+5 -> +15`.
- Give coin to beggar with the fix36 companion JSON: visible result `+100`.
  This is enough because `+100` is the game maximum for that action.
- NPC gift affinity: still vanilla `+5`.
- Petting base gain: still vanilla `+5`.
- Pet abyss gear bonus: separate game/gear effect of `+10`, for a visible
  `+15` total when both vanilla petting and gear apply.

Included companion patch:

`release-artifacts/Player_Status_Modifier_Affinity_3x_DMM.json`

Expected behavior with the fix37 companion DMM patch enabled:

- NPC greeting friendly talk gain: `+5 -> +15`.
- NPC donation / beggar coin gain: `+50 -> +100`.
- NPC gift gain remains `+5` until the gift-specific data path or runtime path
  is found.
- Petting base gain remains `+5` until a separate pet-specific data path or
  runtime hook is found.
- Pet abyss gear bonus remains capped at `+10`; this is independent of petting
  base gain.

Implementation notes:

- `DropSet_Friendly_Talk` remains the proven greeting data path in
  `gamedata/dropsetinfo.pabgb`.
- `DropSet_Friendly_Donate` has two confirmed `50` values at absolute offsets
  `222407` and `222415`; fix37 patches both to `100` (`64 00 00 00`).
- The previously researched `DropSet_*_FavoriteItem_Reward` entries are reward
  quantity tables, not confirmed NPC gift affinity amount tables. They are not
  patched in fix37.
- The pet interaction entries found so far do not contain a safe confirmed
  base-gain value to patch. Do not treat unaligned `15`-looking bytes in pet
  interaction payloads as confirmed pet trust constants.

Disable other trust/friendly data patches while testing this one, especially
older `friendly_gain_x20` or Trusty trust presets, so the result is
unambiguous.
