# Fix 30 - affinity friendly hit diagnostics

Build id:

```text
safe-rva-fixed-fix30-affinity-friendly-hit-diagnostics
```

Purpose:
- Keeps the fix29 affinity/friendly callsite hook pair.
- Adds low-volume hit logging before the scaler gate.

Why:
- Fix29 installed both hooks successfully, but the gameplay log did not contain `runtime: scaled friendly ...`.
- The new log rows identify whether the friendly callsites are firing and what value is present at the suspected `_varyFriendly` qword offset `0x20`.

What to look for:
- `hooks: affinity-vary friendly hit ...`
- `hooks: affinity-vary-logout friendly hit ...`
- `delta_q20=...`
- `runtime: scaled friendly path=...` when a positive friendly/trust delta is scaled.
