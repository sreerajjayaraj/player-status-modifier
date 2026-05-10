# Bug Fixes for player-status-modifier

This document lists all bugs found and fixed in the Crimson Desert player-status-modifier mod.

## Bug 1: Tautological return in `InstallDurabilityHooks()`

**File:** `src/hooks/durability_hooks.cpp:188`

**Issue:** The function always returns `true` regardless of whether hooks were successfully installed.

**Original Code:**
```cpp
return installed_all || !g_durability_hook || !g_durability_delta_hook || !g_abyss_durability_delta_hook ? true : true;
```

**Problem:** The ternary operator evaluates to `true` in all cases (`condition ? true : true`). This means the function reports success even when hook installation fails.

**Fixed Code:**
```cpp
return installed_all;
```

**Impact:** CRITICAL - Without this fix, the mod continues running even when durability hooks fail to install, leading to undefined behavior.

---

## Bug 2: Incorrect variable declaration for `g_reported_affinity_current_exception`

**Files:** 
- `src/hooks/affinity_hooks.cpp:16`
- `src/hooks/hooks_internal.h` (missing extern)
- `src/hooks/install_hooks.cpp` (missing definition)

**Issue:** `g_reported_affinity_current_exception` is declared as a local static variable inside an anonymous namespace in `affinity_hooks.cpp`, but it's used like a global exception flag similar to all other `g_reported_*_exception` variables.

**Original Code (affinity_hooks.cpp):**
```cpp
namespace {
std::atomic<bool> g_reported_affinity_current_exception{false};
std::atomic<std::uint32_t> g_affinity_current_samples{0};
```

**Problem:** Inconsistent with the pattern used for all other exception flags. Should be declared `extern` in `hooks_internal.h` and defined in `install_hooks.cpp`.

**Fixed Code:**
- Removed from `affinity_hooks.cpp` anonymous namespace
- Added `extern std::atomic<bool> g_reported_affinity_current_exception;` to `hooks_internal.h`
- Added `std::atomic<bool> g_reported_affinity_current_exception{false};` to `install_hooks.cpp`

**Impact:** MEDIUM - Compilation may succeed but the variable is isolated to one translation unit instead of being globally accessible.

---

## Bug 3: Health consumption multiplier never applied

**Files:**
- `src/runtime/damage_logic.cpp:247-263` (fixed)
- `src/config.h` (partial fix in Bug 4)

**Issue:** The `Health.ConsumptionMultiplier` config value is read from the INI file and stored, but never actually applied to incoming player damage. The code only applies `health.heal_multiplier` for positive health deltas and uses `config.damage.incoming` for negative deltas, ignoring the health consumption setting entirely.

**Original Code:**
```cpp
if (target == player_target_owner) {
    channel = config.damage.incoming;  // Wrong! Should use health.consumption_multiplier
    direction = "incoming-damage";
}
```

**Problem:** Players configuring `Health.ConsumptionMultiplier=0.5` (take 50% damage) see no effect because the mod uses `IncomingDamage.Multiplier` instead.

**Fixed Code:**
```cpp
if (target == player_target_owner) {
    // Incoming damage to player should use health.consumption_multiplier
    if (config.health.consumption_multiplier == 1.0) {
        return false;
    }
    const double scaled = std::floor(static_cast<double>(*value) * config.health.consumption_multiplier);
    // ... scale and apply ...
    Log("... multiplier=%.3f", config.health.consumption_multiplier);
    return true;
}
```

**Impact:** HIGH - Major feature completely non-functional. Users cannot reduce incoming damage using the documented `Health.ConsumptionMultiplier` setting.

---

## Bug 4: Missing health consumption check in `ShouldInstallDamageHook()`

**File:** `src/config.h:170`

**Issue:** The damage hook installation check doesn't include `health.consumption_multiplier`, so if a user *only* sets `Health.ConsumptionMultiplier` (without enabling `IncomingDamage` or other damage features), the damage hook never installs and the setting has no effect.

**Original Code:**
```cpp
inline bool ShouldInstallDamageHook(const ModConfig& config) {
    return config.general.enabled &&
           ((config.damage.outgoing.enabled && config.damage.outgoing.multiplier != 1.0) ||
            (config.damage.incoming.enabled && config.damage.incoming.multiplier != 1.0) ||
            (config.mount.enabled && config.mount.lock_health) ||
            config.health.heal_multiplier != 1.0);
    // Missing: || config.health.consumption_multiplier != 1.0
}
```

**Fixed Code:**
```cpp
inline bool ShouldInstallDamageHook(const ModConfig& config) {
    return config.general.enabled &&
           ((config.damage.outgoing.enabled && config.damage.outgoing.multiplier != 1.0) ||
            (config.damage.incoming.enabled && config.damage.incoming.multiplier != 1.0) ||
            (config.mount.enabled && config.mount.lock_health) ||
            config.health.heal_multiplier != 1.0 ||
            config.health.consumption_multiplier != 1.0);
}
```

**Impact:** HIGH - Combined with Bug 3, this completely breaks health consumption multiplier functionality.

---

## Bug 5: Unprotected memory reads in damage participant tracking

**File:** `src/runtime/damage_logic.cpp`
- Lines 59-76 (`IsRelatedDamageParticipant`)
- Lines 103-127 (`IsOutgoingPlayerDamageSource`)

**Issue:** Multiple raw pointer dereferences without SEH (Structured Exception Handling) protection. If the game passes invalid pointers, the mod crashes instead of gracefully handling the error.

**Original Code (IsRelatedDamageParticipant):**
```cpp
for (size_t index = 0; index < related.size(); ++index) {
    const uintptr_t nested = *reinterpret_cast<const uintptr_t*>(candidate + index * sizeof(uintptr_t));
    // Can crash if candidate points to unmapped memory
    related[index] = nested;
}
```

**Original Code (IsOutgoingPlayerDamageSource):**
```cpp
const uintptr_t source_actor = *reinterpret_cast<const uintptr_t*>(source_context + 0x68);
// Can crash if source_context is invalid
const uintptr_t source_marker = *reinterpret_cast<const uintptr_t*>(source_actor + 0x20);
// Can crash if source_actor is invalid
```

**Fixed Code:**
```cpp
__try {
    for (size_t index = 0; index < related.size(); ++index) {
        const uintptr_t nested = *reinterpret_cast<const uintptr_t*>(candidate + index * sizeof(uintptr_t));
        related[index] = nested;
        if (IsTrackedDamageParticipant(nested)) {
            return true;
        }
    }
} __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
}
```

Similar SEH blocks added to all memory reads in `IsOutgoingPlayerDamageSource`.

**Impact:** MEDIUM-HIGH - Can cause random crashes during gameplay when the game passes unexpected pointer values.

---

## Summary

**5 bugs fixed:**
1. ✅ Tautological return (CRITICAL)
2. ✅ Incorrect affinity exception variable scope (MEDIUM)
3. ✅ Health consumption multiplier not applied (HIGH)
4. ✅ Missing hook installation condition (HIGH)
5. ✅ Unprotected memory reads (MEDIUM-HIGH)

**Files modified:**
- `src/hooks/durability_hooks.cpp`
- `src/hooks/affinity_hooks.cpp`
- `src/hooks/hooks_internal.h`
- `src/hooks/install_hooks.cpp`
- `src/runtime/damage_logic.cpp`
- `src/config.h`

All fixes maintain compatibility with existing configuration files and the documented behavior in README.md and AGENTS.md.
