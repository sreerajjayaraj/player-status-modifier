# Fix33 affinity item-event stack map

Fix33 keeps the fix32 affinity item-event probes, but expands their diagnostics.

The fix32 gameplay log proved that the `OnFriendlyItem_Give` and
`OnFriendlyItem_Take` callsites are valid and safe to hook.  The first pass
decoded the fifth argument from `ctx.rsp + 0x20`, but SafetyHook's saved stack
view for this callsite did not line up with that guess, so the event pointer
logged as null.

This build logs:

- `r14` as a direct event-name candidate.
- `ctx.rsp` and `ctx.trampoline_rsp`.
- event-name candidates from both stack views.
- stack qwords from `rsp - 0x20` through `rsp + 0x60`.
- nearby `rbp` fields used by the give/take event call sequence.

No affinity values are written by these probes.  This is a diagnostic-only
build intended to map the live callsite safely before adding any relationship
multiplier logic there.
