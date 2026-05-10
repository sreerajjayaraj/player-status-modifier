# Fix39 - broad friendly data diagnostic

Build status:

`fix39-data-only`

The ASI is unchanged from fix38:

`safe-rva-fixed-fix38-damage-only-affinity100`

Purpose:

- Add a broader DMM data diagnostic for unresolved relationship paths.
- Keep the failed pet-gear route clearly separated from the main test path.
- Test whether unnamed `dropsetinfo` friendly rows cover NPC gifts or
  character-specific friendly rewards.

User gameplay result that triggered this:

- With only `Player Status Modifier - Affinity 100 + Pet Gear Diagnostic`
  enabled, NPC greeting reached `+100`.
- Petting still stayed at `+15`, meaning the pet abyss-gear bonus row is not
  the missing petting base-affinity route.
- Damage was not tested in that run.

New companion patch:

`release-artifacts/Player_Status_Modifier_Affinity_100_BroadFriendly_DMM.json`

Patch details:

- Targets `gamedata/dropsetinfo.pabgb`.
- Patches every positive `unk4 == 7` friendly amount candidate found in the
  v1.05.01 `dropsetinfo` scan from `5`, `25`, or `50` to `100`.
- Covers 115 rows / 230 byte changes.
- Includes the proven `DropSet_Friendly_Talk` greeting and
  `DropSet_Friendly_Donate` donation rows.
- Adds absolute-offset patches for 113 unnamed `50/50` friendly rows and one
  unnamed `25/25` friendly row.
- Skips `DropSet_Friendly_Threat`, because its amount is a negative friendly
  value and must not be converted to positive `100`.

Expected test behavior:

- NPC greeting should remain `+100`.
- Beggar coin donation should remain `+100`.
- NPC gift affinity may change if gifts use one of the unnamed friendly rows.
- Petting may still remain `+15`; if it does, petting needs a live executable
  hook or trace rather than another pet-gear/dropset patch.

Testing instruction:

Enable only this JSON affinity patch for the next run. Disable:

- `Player Status Modifier - Affinity 100 Data Patch`
- `Player Status Modifier - Affinity 100 + Pet Gear Diagnostic`
- Trusty / other relationship data patches

Report greeting, gift, donation, and petting as separate values.
