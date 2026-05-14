# Fix50 - stamina/spirit diagnostics and spirit stat-write fallback

Build id: `safe-rva-fixed-fix50-stamina-spirit-diagnostics`

Changes:
- Added low-volume diagnostic logging for stamina and spirit events that are observed but not adjusted.
- Added explicit log reasons for zero-delta events, non-matching stat types, untracked entries, and scaling that results in the same value.
- Extended the stat-write fallback hook installation to include non-neutral `[Spirit]` settings.
- Added `PlayerSpirit` handling to the stat-write adjustment path, matching the existing health/stamina fallback behavior.
- Expanded stat-write callback logs to include the tracked player spirit entry.

Purpose:
- Helps diagnose reports where stamina/spirit appear inactive because another mod has already negated consumption, because the game sends zero deltas, or because the current game build routes spirit through stat writes instead of the spirit-delta callsite.
