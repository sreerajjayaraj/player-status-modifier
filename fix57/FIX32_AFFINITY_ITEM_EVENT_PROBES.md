# Fix 32 - affinity item event probes

Build id:

```text
safe-rva-fixed-fix32-affinity-item-event-probes
```

Purpose:
- Keeps the fix31 affinity scaler and logout `r9` correction.
- Adds diagnostic-only hooks for the `OnFriendlyItem_Give` and `OnFriendlyItem_Take` event payload callsites.

Why:
- Fix31 is stable, but the currently hooked `AIFunction_VaryFriendly` routes did not fire during gameplay.
- Static PE scanning found live-looking event payload callsites near the `OnFriendlyItem_Give` and `OnFriendlyItem_Take` strings.
- These probes log the event name pointer, argument registers, and small pointed values without changing game behavior.

What to look for:
- Startup build id `safe-rva-fixed-fix32-affinity-item-event-probes`.
- Startup rows for `affinity-item-give-event` and `affinity-item-take-event` callsite installation.
- Runtime rows like `hooks: affinity-item-event give ...` or `hooks: affinity-item-event take ...` when a relationship item route is exercised.
