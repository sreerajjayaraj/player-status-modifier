# Fix 29 - affinity logout callsite

Build id:

```text
safe-rva-fixed-fix29-affinity-logout-callsite
```

Purpose:
- Keeps the fix28 copy-on-write friendly/trust scaler.
- Corrects the `AIFunction_VaryFriendlyWithLogout` virtual dispatch callsite RVA from `0x01319B8D` to `0x01319B8B`.

Why:
- Fix28 reached the normal `AIFunction_VaryFriendly` callsite, but the logout variant failed exact-byte validation because the RVA pointed two bytes into the call instruction.
- With this fix, both friendly callsite hooks should install as a pair.

Expected startup log:
- `scanner: affinity-vary-friendly callsite found at hardcoded RVA 0x0F995548`
- `scanner: affinity-vary-friendly-with-logout callsite found at hardcoded RVA 0x01319B8B`
- `hooks: installed affinity-vary-friendly hook`
- `hooks: installed affinity-vary-logout-friendly hook`
- `hooks: affinity friendly scaler hooks installed with copy-on-write data`
