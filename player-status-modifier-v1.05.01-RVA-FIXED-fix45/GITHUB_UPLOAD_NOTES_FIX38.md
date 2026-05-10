# GitHub upload notes - fix38

Build id:

`safe-rva-fixed-fix38-damage-only-affinity100`

Main changes:

- Fixes the outgoing damage reduction dead-end where enemies could stop losing
  health at low values.
- Caps damage-hook combat logging to reduce combat lag.
- Adds `UseStatWriteFallback=0/1` to `[OutgoingDamage]` and
  `[IncomingDamage]`; default is off.
- Adds `Enabled=0/1` to `[Health]`, `[Stamina]`, and `[Spirit]`.
- Adds `player-status-modifier.damage-only.ini` for users who only want
  outgoing damage nerfing.
- Updates the standard affinity companion JSON so greeting and donation both
  target `+100`.
- Includes a separate pet-gear diagnostic JSON that was tested after packaging:
  greeting still reaches `+100`, but petting remains `+15`, so this route is
  not effective for petting affinity.

Known open items:

- NPC gift affinity is still not multiplied. Gift values vary by gift, so this
  needs a real multiplier hook or the correct gift amount table.
- True petting base affinity is still not patched. The pet-gear data route was
  tested and confirmed not to raise petting above `+15`.

Recommended damage-only test:

- Use `player-status-modifier.damage-only.ini`.
- Keep `[OutgoingDamage] UseStatWriteFallback=0` first.
- If a specific outgoing damage type is missed, test again with
  `UseStatWriteFallback=1`; fix38 keeps fallback damage lethal with minimum
  damage `1` when the multiplier is above zero.
