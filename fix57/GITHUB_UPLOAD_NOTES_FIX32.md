# GitHub upload notes - fix32

Use the fix32 GitHub-ready folder as the upload source.

Core change:
- Adds diagnostic-only probes for the `OnFriendlyItem_Give` and `OnFriendlyItem_Take` event payload callsites.
- Keeps fix31's affinity scaler and logout `r9` correction unchanged.

Validation targets:
- Startup should show build id `safe-rva-fixed-fix32-affinity-item-event-probes`.
- Startup should show `affinity-vary=1 affinity-logout=1`.
- Startup should show installed `affinity-item-give-event-probe` and `affinity-item-take-event-probe`.
- Relationship item gameplay should produce `hooks: affinity-item-event ...` rows.
