# Fix 11B - Outgoing Damage Compile Fix

This package is based on fix-11.

It fixes the Visual Studio compile error:

```text
'GetTrackedPlayerHealthEntry': identifier not found
```

Cause: `src/runtime/stat_logic.cpp` used `GetTrackedPlayerHealthEntry()` for the new outgoing-damage stat-write path, but the file did not include `mod_logic.h`, where that function is declared.

Runtime logic is unchanged from fix-11.
