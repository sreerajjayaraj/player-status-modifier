# GitHub upload notes - fix33

Build id:

`safe-rva-fixed-fix33-affinity-item-event-stack-map`

Purpose:

- Keep the stable fix32 startup, mount, damage, item, stamina, spirit, and
  durability behavior.
- Preserve the affinity vary/logout hooks.
- Expand the diagnostic-only `OnFriendlyItem_Give` / `OnFriendlyItem_Take`
  probes with stack/register mapping after the fix32 gameplay log showed the
  callsites fire safely but the first stack argument guess decoded as null.

Expected log lines:

- `hooks: affinity-item-event give regs ...`
- `hooks: affinity-item-event give args ...`
- `hooks: affinity-item-event give stack ...`
- matching `take` lines when the take path runs.

These probes do not change relationship values yet.
