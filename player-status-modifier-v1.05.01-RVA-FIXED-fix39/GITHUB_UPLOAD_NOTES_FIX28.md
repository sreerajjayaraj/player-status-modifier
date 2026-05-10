# GitHub upload notes - fix28

Use the fix28 GitHub-ready folder as the upload source.

Core change:
- Replaces the fix27 read-only friendly diagnostics with copy-on-write friendly/trust scaling hooks.
- The new hooks target the `AIFunction_VaryFriendly` and `AIFunction_VaryFriendlyWithLogout` virtual dispatch call sites.
- The scaler copies the relationship data, scales the suspected `_varyFriendly` field at object offset `0x20`, then passes the copy into the game call.

Validation targets:
- Startup should show build id `safe-rva-fixed-fix28-affinity-friendly-copy-scaler`.
- Startup should install `affinity-vary-friendly` and `affinity-vary-logout-friendly` when `[Affinity] Multiplier` is not `1.0`.
- Gameplay relationship events should log `runtime: scaled friendly path=...`.
- NPC relationship and pet/animal trust gains should be checked separately.

Deployment:
- Keep `player-status-modifier.asi` under the Definitive Mod Manager `mods` folder.
- Do not keep a loose `player-status-modifier.asi` in `bin64`.
