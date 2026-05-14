# Fix38 - damage-only controls and affinity 100 JSON

Build id:

`safe-rva-fixed-fix38-damage-only-affinity100`

Purpose:

- Address fix37 reports of no damage, enemies not dying, and combat lag.
- Add clear INI switches for users who only want outgoing/incoming damage.
- Fold the verified affinity JSON result into the shipped companion patch:
  greeting `+5 -> +100` and donation `+50 -> +100`.

Damage changes:

- Damage hook logging is now sampled instead of writing every combat event.
- `[OutgoingDamage] UseStatWriteFallback=0` is the new default. This keeps
  damage-only configs on the narrower damage hook and avoids the broad health
  stat-write fallback unless a user explicitly enables it.
- `[IncomingDamage] UseStatWriteFallback=0` is also the default.
- If the outgoing stat-write fallback is enabled and the multiplier is above
  zero, reduced damage now has a minimum of `1`, so enemies can still die when
  their health is low.
- `[Health]`, `[Stamina]`, and `[Spirit]` now accept `Enabled=0`. Disabled stat
  sections are sanitized to neutral `1.0/1.0` multipliers before hooks are
  selected.

New preset:

`player-status-modifier.damage-only.ini`

This preset enables only outgoing damage reduction, disables health/stamina/
spirit/item/affinity/durability/mount/dragon extras, and keeps stat-write
fallback disabled.

Affinity companion JSON:

- `release-artifacts/Player_Status_Modifier_Affinity_3x_DMM.json`
  - Standard file despite the historical filename.
  - NPC greeting: `+5 -> +100`.
  - Donation / beggar coin: `+50 -> +100`.
  - Pet abyss gear bonus remains `10`.
- `release-artifacts/Player_Status_Modifier_Affinity_100_PetGear_100_DMM.json`
  - Failed/diagnostic-only pet option.
  - Attempts petting-with-abyss-gear total `+100` by setting abyss gear pet
    bonus to `95`.
  - Gameplay result on 2026-05-10: with only this JSON enabled, NPC greeting
    reached `+100`, but petting still stayed at `+15`. Treat this route as
    confirmed ineffective for petting affinity.

Still open:

- NPC gift affinity needs a true multiplier path. The researched
  `DropSet_*_FavoriteItem_Reward` tables are reward item quantity tables, not
  confirmed gift affinity amount tables, so fix38 does not patch them.
- True petting base affinity remains unresolved. The separate pet-gear route
  was tested and did not change the visible `+15` petting result.
