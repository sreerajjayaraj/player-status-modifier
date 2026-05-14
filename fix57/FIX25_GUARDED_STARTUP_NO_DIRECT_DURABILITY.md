# Fix 25 - Guarded Startup and Direct Durability Hook Bypass

Build id:

```text
safe-rva-fixed-fix25-guarded-startup-no-direct-durability
```

This build addresses the startup crash seen with fix23 after the ASI was enabled through Definitive Mod Manager.

Observed crash boundary:

```text
hooks: durability callback entry=... old=6939 requested=0
runtime: skipped maintenance consumption ...
hooks: durability adjusted ...
```

The log reached `dllmain: initialization finished`, then stopped immediately after the direct durability maintenance-write hook fired. That makes the direct durability hook the highest-risk startup suspect.

Changes:

- Adds a guarded initialization wrapper around the ASI startup thread.
- Raises the effective startup hook delay to at least 10000 ms, even when the INI has `InitDelayMs=3000`.
- Logs SEH/C++ initialization exceptions and disables the mod for that process instead of allowing initialization faults to crash the game.
- Disables the direct durability maintenance-write hook for startup stability.
- Keeps the two durability delta hooks active, because they handled normal durability wear events in previous gameplay logs without being at the crash boundary.
- Keeps the fix24 cached ground-mount liveness behavior for White Bear / ground mount tracking.

Expected log differences:

```text
dllmain: startup guard raised init delay from 3000 ms to 10000 ms
hooks: durability maintenance write hook disabled for startup stability; delta hooks remain active
hooks: installed durability-delta hook ...
hooks: installed abyss-durability-delta hook ...
```

