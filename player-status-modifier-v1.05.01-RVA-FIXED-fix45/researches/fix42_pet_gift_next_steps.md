# Fix42 petting and gift affinity next steps

Status after fix39/fix42 data-patch testing:

- The broad-friendly DMM JSON can raise NPC greeting affinity to `+100`.
- Beggar coin donation can reach the `+100` cap.
- Petting affinity still follows vanilla / gear behavior and was not changed by
  the broad-friendly JSON.
- NPC gift affinity is not yet confirmed as controlled by the same friendly data
  rows. Some gifts may still use a separate item/event route.

Unsafe path excluded:

- The external fix41 pet-hook experiment installed live `affinity-pet-*` hooks.
- Runtime logs showed the tested pet hook reading impossible values from the
  presumed argument/offset path, so it is not safe to ship.
- Fix42 intentionally excludes those hooks.

Recommended next research:

- Trace `AdditionalPetFriendlyValue`, `AddPetFriendly`, and
  `PetFriendlyReached` in x64dbg/Ghidra to identify the real petting delta
  source before any write hook is attempted.
- Trace `OnFriendlyItem_Give` and `OnFriendlyItem_Take` during real NPC gift
  actions to identify whether gifts share the friendly data table or use a
  separate per-item/event value.
- If a diagnostic ASI is needed, keep it logging-only and config-gated by
  default. Do not patch live pet/gift values until the real delta register or
  memory field has been confirmed from logs.
