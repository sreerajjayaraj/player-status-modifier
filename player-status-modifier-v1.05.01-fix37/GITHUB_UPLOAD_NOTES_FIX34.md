# GitHub upload notes - fix34

Build id:

`safe-rva-fixed-fix34-item-gain-affinity-diagnostics`

Purpose:

- Stable continuation of fix33.
- Adds diagnostic context around the item-gain instruction that fired during
  the latest NPC/animal affinity gameplay run.
- Does not attempt to change affinity values yet.

Why:

- The legacy affinity prepare/current pair is still rejected safely on 1.05.01.
- The affinity vary/logout hooks install, but did not fire during the latest
  NPC/animal affinity test.
- `OnFriendlyItem_Give` / `OnFriendlyItem_Take` probes only fired at startup,
  which makes them event registration, not the runtime relationship update.
- Item-gain callbacks did fire during the interaction window, so fix34 captures
  enough memory/register context to decide whether that route is usable.

Expected log lines:

- `hooks: item-gain-affinity diagnostic ...`
- `hooks: item-gain-affinity window ...`
- `hooks: item-gain-affinity regs ...`
