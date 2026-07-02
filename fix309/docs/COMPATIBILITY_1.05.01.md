# Crimson Desert 1.05.01 Compatibility Notes

This source tree has been adjusted and verified for the uploaded Crimson Desert 1.05.01 executable.

## Verification target

- Executable: `CrimsonDesert.exe` extracted from the uploaded `CrimsonDesert.zip`
- SHA-256: `4a858f3269fcea526f995b33d75a4c723b5df5fbb6a1a44d4a2691aa81629365`
- PE image base: `0x140000000`
- `SizeOfImage`: `0x18762000`

See `CRIMSON_DESERT_1.05.01_SCAN_REPORT.md` for the full signature-count table and resolved RVAs.

## What changed

- Added a `PlayerPointerMarkerRegister` result to the player-pointer scanner.
- Added support for the 1.05.01 player-pointer sequence where the status marker is loaded through `RDX`:
  - `mov rax, [rax+68]`
  - `mov rdx, [rax+20]`
- Kept support for the older `RSI` marker sequences.
- Added exact 1.05.01 patterns for version-sensitive hooks where the uploaded executable confirmed a unique match.
- Relaxed version-sensitive displacement bytes only where the relaxed pattern also resolves uniquely on the uploaded 1.05.01 executable, especially:
  - `call rel32`
  - `jmp rel32`
  - conditional-branch displacements
  - RIP-relative data displacements
- Changed scanner fallback behavior so an ambiguous relaxed pattern no longer immediately aborts the whole scan. It logs the ambiguous variant and continues to the next variant.
- Restricted the scanner's second pass to executable sections instead of every non-`.text` section. The uploaded executable has no conventional `.text` section; its verified hook targets are in executable `.reloc` / `.rsrc` sections.
- Removed the overly broad `RDX` player-pointer fallback because it produced multiple possible sites. The exact and relaxed `RDX` 1.05.01 patterns each resolve once.

## Verified runtime-selected hook RVAs

| Hook scanner | Hook RVA | Notes |
|---|---:|---|
| `ScanForPlayerPointerCapture` | `0x34FB1A` | `marker-source=rdx` |
| `ScanForMountPointerCapture` | `0x487996` |  |
| `ScanForPositionHeightAccess` | `0x381859C` |  |
| `ScanForDamageBattleAccess` | `0x1361B30` |  |
| `ScanForDragonVillageSummonJump` | `0x1C9CDB9` |  |
| `ScanForDragonFlyingRestrictWrite` | `0x1E21E4B` |  |
| `ScanForDragonRoofRestrictTest` | `0x2E2F0A` |  |
| `ScanForItemGainAccess` | `0x1005474B` |  |
| `ScanForAffinityGainPrepare` | `0x327973` |  |
| `ScanForAffinityCurrentWrite` | `0x2D33DC` |  |
| `ScanForDurabilityWriteAccess` | `0xFD2BEA6` |  |
| `ScanForDurabilityDeltaAccess` | `0x6C1D69` |  |
| `ScanForAbyssDurabilityDeltaAccess` | `0x1E037E0` |  |
| `ScanForStaminaAb00Access` | `0x136645E` |  |
| `ScanForSpiritDeltaAccess` | `0xCA21EFB` |  |
| `ScanForStatsAccess` | `0x13619BB` |  |
| `ScanForStatWriteAccess` | `0xCA35593` |  |

## Files changed

- `src/scanner.h`
- `src/scanner.cpp`
- `src/hooks/player_hooks.cpp`
- `COMPATIBILITY_1.05.01.md`
- `CRIMSON_DESERT_1.05.01_SCAN_REPORT.md`

## Build notes

Build the ASI from this source tree with the existing CMake/MSVC workflow. After launch, check `player-status-modifier.log` for:

- `hooks: installed player-pointer hook ... marker-source=rdx`
- no `scanner: ... found 0 matches`
- no `scanner: ... found multiple matches`

This update avoids blind absolute-address patching. The scanner still fails closed and logs a scan failure if a future executable changes instruction semantics enough that the verified signatures no longer resolve safely.
