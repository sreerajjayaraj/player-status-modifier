# Fix 26 - Normal Startup Timing with Direct Durability Hook Bypassed

Build id:

```text
safe-rva-fixed-fix26-normal-startup-no-direct-durability
```

Fix25 confirmed the startup crash was avoided, but its global 10000 ms hook delay caused the core player-pointer hook to fail:

```text
hooks: failed to create player-pointer mid hook
dllmain: hook installation failed
```

Fix26 keeps the startup stability change that matters most, disabling the direct durability maintenance-write hook, but restores the normal INI-driven startup delay so the core hooks install at the same timing that worked in fix23.

Retained:

- SEH/C++ guarded initialization wrapper.
- Direct durability maintenance-write hook bypass.
- Durability delta hooks remain active.
- Cached ground-mount liveness behavior from fix24.

Changed from fix25:

- Removed the forced 10000 ms minimum startup delay.
- Uses `General.InitDelayMs` from the INI again, currently 3000 ms in the game config.

