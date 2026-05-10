# Fix 31 - affinity logout r9 diagnostics

Build id:

```text
safe-rva-fixed-fix31-affinity-logout-r9-diagnostics
```

Purpose:
- Keeps the fix30 friendly hit diagnostics.
- Corrects the `AIFunction_VaryFriendlyWithLogout` live scaler to read and replace the friendly data argument from `r9`.

Why:
- Fix30 installed both friendly hooks and stayed stable during gameplay, but no affinity hit rows appeared.
- Disassembly of the logout-friendly callsite shows the data argument is loaded with `mov r9,r15` immediately before `call r10`.
- Fix30 was reading `r8`, so a real logout-friendly call would not have scaled the intended payload.

What to look for:
- Startup build id `safe-rva-fixed-fix31-affinity-logout-r9-diagnostics`.
- Startup loadout with `affinity-vary=1 affinity-logout=1`.
- Runtime rows such as `hooks: affinity-vary friendly hit ...` or `hooks: affinity-vary-logout-r9 friendly hit ...`.
- Positive friendly/trust deltas should also produce `runtime: scaled friendly path=...`.
