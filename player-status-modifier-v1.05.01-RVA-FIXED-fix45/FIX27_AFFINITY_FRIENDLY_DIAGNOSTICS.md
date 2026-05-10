# Fix 27 - affinity friendly diagnostics

Build id:

```text
safe-rva-fixed-fix27-affinity-friendly-diagnostics
```

Purpose:
- Keeps the fix26 startup and durability behavior unchanged.
- Keeps the old two-site affinity scaler guarded by its exact byte checks.
- Adds read-only diagnostic hooks for the Crimson Desert 1.05.01 friendly/intimacy functions:
  - `AIFunction_VaryFriendly`
  - `AIFunction_VaryFriendlyWithLogout`
- Logs the likely relationship data record fields without modifying relationship values.

What to look for in gameplay logs:
- `hooks: installed affinity-vary-diagnostic hook`
- `hooks: installed affinity-vary-logout-diagnostic hook`
- `hooks: affinity-vary probe ...`
- `hooks: affinity-vary-logout probe ...`

Expected limitation:
- This build does not claim the affinity multiplier is functional yet.
- The diagnostic rows are intended to identify which live field contains the NPC/pet/animal friendly gain before enabling any writes.
