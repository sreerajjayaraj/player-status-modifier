# Runtime fix 13 - player tracking guard

Base: fix-12B.

The fix-12B log showed the health-write fallback correctly tracked the active
player health entry, then a later stats/component callback replaced
`g_player_resolve` with a different resolved health entry. After that, writes to
the original active player health entry were logged as `untracked health write
skipped`.

Fix-13 keeps the stable health-write fallback as authoritative. A later
stats/component resolve is allowed to fill marker/root/stamina/spirit only when
it resolves to the same health entry. Mismatched stats resolves are ignored
instead of replacing the working player health pointer.

Affinity remains unchanged and disabled/rejected.
