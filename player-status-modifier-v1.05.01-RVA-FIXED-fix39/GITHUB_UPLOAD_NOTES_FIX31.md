# GitHub upload notes - fix31

Use the fix31 GitHub-ready folder as the upload source.

Core change:
- Corrects the logout-friendly affinity scaler to use `r9`, matching the 1.05.01 callsite register setup.
- Keeps fix30's low-volume friendly hit diagnostics.

Validation targets:
- Startup should show build id `safe-rva-fixed-fix31-affinity-logout-r9-diagnostics`.
- Startup should show `affinity-vary=1 affinity-logout=1`.
- Relationship/trust routes should produce `hooks: affinity-... friendly hit ...`.
- Positive friendly/trust deltas should also produce `runtime: scaled friendly path=...`.
