# Fix34 - item-gain affinity diagnostics

Build id:

`safe-rva-fixed-fix34-item-gain-affinity-diagnostics`

Purpose:

- Keep the stable fix33 startup, mount, damage, item, stamina, spirit, and
  durability behavior.
- Preserve the diagnostic affinity vary/logout and friendly item event probes.
- Add richer diagnostic logging to the `item-gain` hook because the fix33
  gameplay log showed no runtime affinity-vary hits during NPC/animal affinity
  actions, while item-gain bursts appeared at the interaction timestamps.

What changed:

- The item-gain hook now logs up to 64 player-ready diagnostic samples with:
  - the target address for `add [r8+rdi+0x10], rcx`
  - current and projected target values before the original add runs
  - nearby qword/dword memory around the target
  - general registers and selected stack slots
- The hook still uses the existing item gain multiplier. This build does not
  scale relationship values directly yet.

Expected log markers:

- `hooks: item-gain-affinity diagnostic ...`
- `hooks: item-gain-affinity window ...`
- `hooks: item-gain-affinity regs ...`

Use this build for one short affinity test with multiple NPCs and animals. The
result should tell whether the visible affinity interactions are flowing through
the item-gain storage instruction or whether the real trust/friendly update is
still elsewhere.
