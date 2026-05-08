# Crimson Desert Mod - Game Version Compatibility Update

## Summary

Updated the player-status-modifier mod to work with the new game executable (CrimsonDesert.exe, 386.5 MB, 405,270,424 bytes, dated May 3, 2026).

## Changes Made

### Pattern Scanner Updates

Updated **8 out of 17** AOB (Array of Bytes) patterns in `src/scanner.cpp` that were incompatible with the new game version:

| Hook Name | Status | Old Pattern Length | New Pattern Length | New Address |
|-----------|--------|-------------------|-------------------|-------------|
| **PlayerPointerCapture** | ✅ Updated | 17 bytes (2 variants) | 25 bytes (1 variant) | 0x0034EF12 |
| **MountPointerCapture** | ✅ Updated | 39 bytes | 30 bytes | 0x00486D82 |
| **DragonVillageSummonJump** | ✅ Updated | 16 bytes | 20 bytes | 0x01C9C1B9 |
| **DragonRoofRestrictTest** | ✅ Updated | 22 bytes | 25 bytes | 0x002E230A |
| **AffinityGainPrepare** | ✅ Updated | 31 bytes (2 variants) | 30 bytes (1 variant) | 0x00326D5F |
| **AffinityCurrentWrite** | ✅ Updated | 20 bytes (2 variants) | 25 bytes (1 variant) | 0x002D27DC |
| **DurabilityWriteAccess** | ✅ Updated | 24 bytes (2 variants) | 25 bytes (1 variant) | 0x0F8466A6 |
| **DurabilityDeltaAccess** | ✅ Updated | 21 bytes (2 variants) | 25 bytes (1 variant) | 0x006C1169 |

### Patterns That Still Work

The following **9 patterns** were found in the new executable without modification:

1. **PositionHeightAccess** - 0x0381799C (✓ Compatible)
2. **DamageBattleAccess** - 0x01360F30 (✓ Compatible)
3. **DragonFlyingRestrictWrite** - 0x01E2124B (✓ Compatible)
4. **ItemGainAccess** - 0x0FB6EF4B (✓ Compatible)
5. **AbyssDurabilityDeltaAccess** - 0x01E02BE0 (✓ Compatible)
6. **StaminaAb00Access** - 0x0136585E (✓ Compatible)
7. **SpiritDeltaAccess** - 0x0C53C6FB (✓ Compatible)
8. **StatsAccess** - 0x01360DBB (✓ Compatible)
9. **StatWriteAccess** - 0x0C54FD93 (✓ Compatible)

## Pattern Discovery Methodology

1. **Initial Scan**: Scanned the new executable for all existing patterns → 9/17 found
2. **Targeted Search**: Used characteristic instruction sequences to find candidates:
   - Searched for distinctive byte patterns (e.g., `cmp byte ptr [rdi+94], 0`)
   - Found 50+ candidates for most missing patterns
3. **Pattern Extraction**: Extended context around candidates to create unique patterns
4. **Verification**: Confirmed each new pattern appears exactly once in the executable

## Technical Details

### Updated Pattern Examples

**PlayerPointerCapture** (Old → New):
```
Old: 49 8B 7D 18 49 8B 44 24 40 48 8B 40 68 48 8B 70 20
New: 48 8B 40 68 48 8B 50 20 4C 8D 45 D7 0F B7 52 30 E8 59 2D AE 01 C5 FA 10 45
```
- Changed from player pointer capture via r13/r12 to direct pointer access
- New pattern includes surrounding context for uniqueness

**DurabilityWriteAccess** (Old → New):
```
Old: 66 3B CF 66 0F 4C F9 66 89 7B 50 48 8B 5C 24 20 48 8B 03 33 D2 48 8B CB FF 50 20
New: 66 89 7B 50 40 88 7B 52 40 88 7B 54 EB 03 48 89 FB 48 8B 05 BA BE 15 F6 48
```
- Core durability write (`mov [rbx+50], di`) remains but surrounding code changed
- Only 6 matches found for the distinctive `66 89 7B 50` sequence

**DurabilityDeltaAccess** (Old → New):
```
Old: 0F B7 C7 66 41 03 C5 66 89 45 38 79 0B 33 C0 66 89 45 38 0F B7 C8
New: C1 79 C5 C1 06 66 41 03 C0 66 89 45 EC C4 C1 79 C5 C1 07 66 41 03 C1 66 89
```
- Original `add ax, r13w` pattern not found
- New pattern uses VEX-encoded SIMD operations (`C4` prefix) with AVX instructions
- Shows game now uses more modern CPU features

## Bug Fixes Included

This update also includes all 5 bug fixes from the previous analysis:

1. ✅ Fixed tautological return in `InstallDurabilityHooks()`
2. ✅ Fixed affinity exception variable scope
3. ✅ **Fixed health consumption multiplier** (was never applied)
4. ✅ Added missing hook installation condition
5. ✅ Added SEH protection to memory reads

## Installation

1. Extract the updated source code
2. Build using Visual Studio 2022 or compatible compiler
3. Copy `dinput8.dll` to the game directory
4. Configure `config.ini` as needed

## Compatibility

- **Game Version**: CrimsonDesert.exe (May 3, 2026 build)
- **File Size**: 386.5 MB (405,270,424 bytes)
- **Compiler**: MSVC 2022 (C++17 or later)
- **Platform**: Windows x64

## Testing Recommendations

1. Test all hook categories:
   - Player pointer capture and mount pointer tracking
   - Damage modification (incoming/outgoing)
   - Durability changes (equipment and abyss)
   - Affinity gain/loss
   - Dragon flight restrictions
   - Stamina and spirit modifications

2. Verify config.ini settings work as expected
3. Check logs for any pattern scan failures
4. Test with various character builds and equipment

## Known Limitations

- The mod requires exact game version matching
- Future game updates will likely require new pattern updates
- Some patterns are version-specific and may need adjustment for regional variants

## Files Modified

- `src/scanner.cpp` - Updated 8 AOB patterns
- `src/hooks/durability_hooks.cpp` - Bug fix #1
- `src/hooks/affinity_hooks.cpp` - Bug fix #2
- `src/hooks/hooks_internal.h` - Bug fix #2
- `src/hooks/install_hooks.cpp` - Bug fix #2
- `src/runtime/damage_logic.cpp` - Bug fixes #3, #5
- `src/config.h` - Bug fix #4

## Credits

Pattern scanning and analysis performed using custom Python tools:
- Fast mmap-based AOB scanner
- Targeted pattern search using distinctive subsequences
- Pattern uniqueness verification

All patterns verified to have exactly one match in the new executable.
---

## 1.05.01 Verified Compatibility Layer

Compatibility work was added and verified against the uploaded Crimson Desert 1.05.01 executable:

- Player pointer capture now supports both older `RSI` marker capture and the 1.05.01 `RDX` marker capture.
- Exact 1.05.01 AOBs are tried before relaxed variants for hooks with version-sensitive relative/displacement bytes.
- Relative `call`, `jmp`, branch, and RIP-relative bytes were wildcarded only where the relaxed variant still resolved uniquely on the uploaded executable.
- Scanner fallback behavior now continues after an ambiguous relaxed pattern, allowing narrower fallback variants to resolve the target.
- The second scanner pass is limited to executable sections, which supports this executable's nonstandard code sections without scanning data sections.
- The hook log records the selected player marker source (`marker-source=rdx` or `marker-source=rsi`).

See `CRIMSON_DESERT_1.05.01_SCAN_REPORT.md` for the verified hook RVAs and full signature-count table.
