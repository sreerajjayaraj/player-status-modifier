# Fix36 - affinity donation patch and pet-base correction

Superseded by fix37: gameplay showed that donation affinity caps at `+100`.
Fix37 patches donation directly to `100` instead of the earlier `150` target.

Build id:

`safe-rva-fixed-fix36-affinity-donate-pet-base-open`

Purpose:

- Keep the stable fix35 ASI behavior and DMM companion patch workflow.
- Correct the pet affinity expectation from fix35: petting base gain is still
  not affected by the ASI or DMM JSON.
- Add the next confirmed data-driven affinity value: NPC donation gain.

Confirmed from gameplay:

- NPC greeting with the companion DMM JSON enabled: `+5 -> +15`.
- Petting base gain: still vanilla `+5`.
- Pet abyss gear bonus: separate game/gear effect of `+10`, for a visible
  `+15` total when both vanilla petting and gear apply.
- Give coin to beggar before fix36: still vanilla `+50`.

Included companion patch:

`release-artifacts/Player_Status_Modifier_Affinity_3x_DMM.json`

Expected behavior with the fix36 companion DMM patch enabled:

- NPC greeting friendly talk gain: `+5 -> +15`.
- NPC donation / beggar coin gain: `+50 -> +150`.
- Petting base gain remains `+5` until a separate pet-specific data path or
  runtime hook is found.
- Pet abyss gear bonus remains capped at `+10`; this is independent of petting
  base gain.

Implementation notes:

- `DropSet_Friendly_Talk` remains the proven greeting data path in
  `gamedata/dropsetinfo.pabgb`.
- `DropSet_Friendly_Donate` was verified in the live parser with two `50`
  values at absolute offsets `222407` and `222415`; fix36 patches both to
  `150`.
- The pet interaction entries found so far do not contain a safe confirmed
  base-gain value to patch. Do not treat unaligned `15`-looking bytes in pet
  interaction payloads as confirmed pet trust constants.

Disable other trust/friendly data patches while testing this one, especially
older `friendly_gain_x20` or Trusty trust presets, so the result is
unambiguous.
