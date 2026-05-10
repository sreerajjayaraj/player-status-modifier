# GitHub upload notes - fix29

Use the fix29 GitHub-ready folder as the upload source.

Core change:
- Corrects the `AIFunction_VaryFriendlyWithLogout` callsite RVA used by the fix28 affinity/friendly scaler.
- The fix28 scaler design is unchanged: copy the friendly data, scale the suspected `_varyFriendly` qword at object offset `0x20`, and pass the copy into the game call.

Validation targets:
- Startup should show build id `safe-rva-fixed-fix29-affinity-logout-callsite`.
- Startup should install both `affinity-vary-friendly` and `affinity-vary-logout-friendly`.
- The hook loadout should show `affinity-vary=1 affinity-logout=1`.
- Relationship/trust gain events should log `runtime: scaled friendly path=...`.
