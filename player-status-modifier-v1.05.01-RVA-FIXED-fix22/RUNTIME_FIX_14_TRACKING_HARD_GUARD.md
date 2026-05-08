# Runtime Fix 14 - Tracking hard guard

Base: fix-13.

The fix-13 guard protected the stats bootstrap path, but the player-pointer/status-component
path could still replace `g_player_resolve` after health-write fallback had already identified
the active player health entry.

Fix-14 adds the same guard to `UpdateTrackedPlayerStatusComponent()` and prevents
`TryAssignPlayerResolvedEntry()` from replacing an already-tracked player health entry.

Expected log during the same scenario:

```text
runtime: ignored player status update with different health current_health=0x... resolved_health=0x...
```

The original player health entry should continue to produce:

```text
hooks: stat-write callback ... matched=1
runtime: adjusted stat write stat=health ...
```

Affinity remains untouched and disabled/rejected exactly like the stable fix-13 baseline.
