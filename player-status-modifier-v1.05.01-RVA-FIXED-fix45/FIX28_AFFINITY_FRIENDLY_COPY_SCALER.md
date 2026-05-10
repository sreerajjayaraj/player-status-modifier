# Fix 28 - affinity friendly copy scaler

Build id:

```text
safe-rva-fixed-fix28-affinity-friendly-copy-scaler
```

Purpose:
- Keeps the fix26 startup and durability behavior unchanged.
- Keeps the old two-site affinity scaler guarded by exact byte checks.
- Adds a new fallback scaler for the Crimson Desert 1.05.01 friendly/trust paths:
  - `AIFunction_VaryFriendly`
  - `AIFunction_VaryFriendlyWithLogout`
- Hooks the virtual dispatch call sites after the game has placed the friendly data pointer into the live argument register.
- Copies the friendly data to thread-local scratch memory, scales the suspected `_varyFriendly` signed qword at object offset `0x20`, and passes the copy to the game call.

Why copy instead of writing the source record:
- The external save/mod editor research repo names the game relationship system as `Friendly` / `PetFriendly`.
- Its dropset parser stores friendly drop payloads as 28 bytes, which matches the executable-side `DropFriendlyData` object layout where raw payload offset `0x18` maps to object offset `0x20`.
- The hook avoids mutating the original game-data record, so repeated relationship events should not keep multiplying the same source value.

What to look for in gameplay logs:
- `hooks: installed affinity-vary-friendly hook`
- `hooks: installed affinity-vary-logout-friendly hook`
- `hooks: affinity friendly scaler hooks installed with copy-on-write data`
- `runtime: scaled friendly path=...`
- `hooks: affinity-... scaled friendly copy ...`

Expected limitation:
- Positive friendly/trust deltas are scaled. Negative deltas are left unchanged.
- This is still a gameplay-test build; verify with NPC relationship gain and pet/animal trust gain.
