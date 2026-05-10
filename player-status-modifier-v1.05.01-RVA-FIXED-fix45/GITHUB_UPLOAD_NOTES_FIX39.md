# GitHub upload notes - fix39 data diagnostic

Fix39 is data-only. The ASI remains the fix38 build:

`safe-rva-fixed-fix38-damage-only-affinity100`

New file:

- `release-artifacts/Player_Status_Modifier_Affinity_100_BroadFriendly_DMM.json`

What changed:

- Adds a broad affinity diagnostic JSON for DMM.
- Patches all positive v1.05.01 `dropsetinfo` friendly amount candidates from
  `5`, `25`, or `50` to `100`.
- Keeps the proven greeting and donation results.
- Adds unnamed friendly rows that may cover NPC gift or character-specific
  affinity rewards.
- Does not include the failed pet-gear `95` route.

Known status:

- Greeting: confirmed `+100` with the earlier affinity JSON.
- Donation: confirmed enough at `+100`, the observed action cap.
- Petting: still unresolved. The pet-gear diagnostic did not change `+15`.
- Damage-only behavior still needs a separate gameplay test using
  `player-status-modifier.damage-only.ini`.

Testing:

- Enable only `Player Status Modifier - Affinity 100 Broad Friendly Diagnostic`.
- Disable other relationship JSON patches while testing.
- Check NPC greeting, NPC gift, donation, and petting separately.
