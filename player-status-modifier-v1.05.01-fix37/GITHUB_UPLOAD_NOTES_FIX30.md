# GitHub upload notes - fix30

Use the fix30 GitHub-ready folder as the upload source.

Core change:
- Adds first-128-sample friendly callsite hit diagnostics to fix29.
- Keeps the copy-on-write scaler unchanged.

Validation targets:
- Startup should show build id `safe-rva-fixed-fix30-affinity-friendly-hit-diagnostics`.
- Startup should show `affinity-vary=1 affinity-logout=1`.
- Relationship/trust routes should produce `hooks: affinity-... friendly hit ...`.
- Positive friendly/trust deltas should also produce `runtime: scaled friendly path=...`.
