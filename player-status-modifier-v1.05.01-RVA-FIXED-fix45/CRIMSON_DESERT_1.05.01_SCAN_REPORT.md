# Crimson Desert 1.05.01 Signature Verification Report

Verified against the uploaded `CrimsonDesert.exe` extracted from `CrimsonDesert.zip`.

- SHA-256: `4a858f3269fcea526f995b33d75a4c723b5df5fbb6a1a44d4a2691aa81629365`
- Image base from PE header: `0x140000000`
- SizeOfImage: `0x18762000`
- Executable sections scanned: `.reloc` RVA `0x1000` raw size `0x45B1000`, `.rsrc` RVA `0x668F000` raw size `0x11C59C00`

The runtime scanner first tries `.text`, then all executable sections, then the full image. This executable does not expose a conventional `.text` section; the verified matches below are in executable sections. A cross-check found no current signature matches in non-executable sections.

## Runtime-selected signatures

| Scanner | Selected pattern | Section | Match RVA | Hook offset | Hook RVA | Notes |
|---|---|---|---:|---:|---:|---|
| `ScanForPlayerPointerCapture` | `rax-rdx-1.05.01-exe` | `.reloc` | `0x34FB12` | `8` | `0x34FB1A` | RDX marker |
| `ScanForMountPointerCapture` | `primary-1.05.01-exe` | `.reloc` | `0x487982` | `20` | `0x487996` |  |
| `ScanForPositionHeightAccess` | `primary` | `.reloc` | `0x3818586` | `22` | `0x381859C` |  |
| `ScanForDamageBattleAccess` | `primary` | `.reloc` | `0x1361B30` | `0` | `0x1361B30` |  |
| `ScanForDragonVillageSummonJump` | `primary-1.05.01-exe` | `.reloc` | `0x1C9CDB4` | `5` | `0x1C9CDB9` |  |
| `ScanForDragonFlyingRestrictWrite` | `primary` | `.reloc` | `0x1E21E4B` | `0` | `0x1E21E4B` |  |
| `ScanForDragonRoofRestrictTest` | `primary-1.05.01-exe` | `.reloc` | `0x2E2F0A` | `0` | `0x2E2F0A` |  |
| `ScanForItemGainAccess` | `primary` | `.rsrc` | `0x1005474B` | `0` | `0x1005474B` |  |
| `ScanForAffinityGainPrepare` | `primary-1.05.01-exe` | `.reloc` | `0x327955` | `30` | `0x327973` |  |
| `ScanForAffinityCurrentWrite` | `primary` | `.reloc` | `0x2D33D7` | `5` | `0x2D33DC` |  |
| `ScanForDurabilityWriteAccess` | `primary-1.05.01-exe` | `.rsrc` | `0xFD2BEA6` | `0` | `0xFD2BEA6` |  |
| `ScanForDurabilityDeltaAccess` | `primary` | `.reloc` | `0x6C1D64` | `5` | `0x6C1D69` |  |
| `ScanForAbyssDurabilityDeltaAccess` | `primary` | `.reloc` | `0x1E037D5` | `11` | `0x1E037E0` |  |
| `ScanForStaminaAb00Access` | `primary` | `.reloc` | `0x1366453` | `11` | `0x136645E` |  |
| `ScanForSpiritDeltaAccess` | `primary` | `.rsrc` | `0xCA21EF5` | `6` | `0xCA21EFB` |  |
| `ScanForStatsAccess` | `primary` | `.reloc` | `0x13619AF` | `12` | `0x13619BB` |  |
| `ScanForStatWriteAccess` | `primary` | `.rsrc` | `0xCA3557F` | `20` | `0xCA35593` |  |

## Full pattern-count scan

| Scanner | Pattern | Matches in executable sections | First RVAs |
|---|---|---:|---|
| `ScanForPlayerPointerCapture` | `rax-rdx-1.05.01-exe` | 1 | `.reloc@0x34FB12` |
| `ScanForPlayerPointerCapture` | `rax-rdx-1.05.01-relaxed` | 1 | `.reloc@0x34FB12` |
| `ScanForPlayerPointerCapture` | `rcx-based` | 1 | `.reloc@0x13A1B1E` |
| `ScanForPlayerPointerCapture` | `rax-fallback-2` | 1 | `.reloc@0x9A5429` |
| `ScanForPlayerPointerCapture` | `rax-fallback-3` | 1 | `.reloc@0x23D1B3D` |
| `ScanForPlayerPointerCapture` | `rax-primary` | 1 | `.reloc@0x9343BE` |
| `ScanForMountPointerCapture` | `primary-1.05.01-exe` | 1 | `.reloc@0x487982` |
| `ScanForMountPointerCapture` | `primary-1.05.01-relaxed` | 1 | `.reloc@0x487982` |
| `ScanForPositionHeightAccess` | `primary` | 1 | `.reloc@0x3818586` |
| `ScanForPositionHeightAccess` | `fallback` | 1 | `.reloc@0x381858F` |
| `ScanForDamageBattleAccess` | `primary` | 1 | `.reloc@0x1361B30` |
| `ScanForDamageBattleAccess` | `fallback` | 1 | `.reloc@0x1361B30` |
| `ScanForDragonVillageSummonJump` | `primary-1.05.01-exe` | 1 | `.reloc@0x1C9CDB4` |
| `ScanForDragonVillageSummonJump` | `primary-1.05.01-relaxed` | 1 | `.reloc@0x1C9CDB4` |
| `ScanForDragonFlyingRestrictWrite` | `primary` | 1 | `.reloc@0x1E21E4B` |
| `ScanForDragonRoofRestrictTest` | `primary-1.05.01-exe` | 1 | `.reloc@0x2E2F0A` |
| `ScanForDragonRoofRestrictTest` | `primary-1.05.01-relaxed` | 1 | `.reloc@0x2E2F0A` |
| `ScanForItemGainAccess` | `primary` | 1 | `.rsrc@0x1005474B` |
| `ScanForAffinityGainPrepare` | `primary-1.05.01-exe` | 1 | `.reloc@0x327955` |
| `ScanForAffinityGainPrepare` | `primary-1.05.01-relaxed` | 1 | `.reloc@0x327955` |
| `ScanForAffinityCurrentWrite` | `primary` | 1 | `.reloc@0x2D33D7` |
| `ScanForDurabilityWriteAccess` | `primary-1.05.01-exe` | 1 | `.rsrc@0xFD2BEA6` |
| `ScanForDurabilityWriteAccess` | `primary-1.05.01-relaxed` | 1 | `.rsrc@0xFD2BEA6` |
| `ScanForDurabilityDeltaAccess` | `primary` | 1 | `.reloc@0x6C1D64` |
| `ScanForAbyssDurabilityDeltaAccess` | `primary` | 1 | `.reloc@0x1E037D5` |
| `ScanForAbyssDurabilityDeltaAccess` | `fallback` | 1 | `.reloc@0x1E037DC` |
| `ScanForStaminaAb00Access` | `primary` | 1 | `.reloc@0x1366453` |
| `ScanForSpiritDeltaAccess` | `primary` | 1 | `.rsrc@0xCA21EF5` |
| `ScanForSpiritDeltaAccess` | `fallback` | 1 | `.rsrc@0xCA21EEF` |
| `ScanForStatsAccess` | `primary` | 1 | `.reloc@0x13619AF` |
| `ScanForStatsAccess` | `fallback` | 5 | `.reloc@0x305366`, `.reloc@0x10FEFEF`, `.reloc@0x13619B3`, `.reloc@0x27540B3`, `.rsrc@0x12F9B272` |
| `ScanForStatWriteAccess` | `primary` | 1 | `.rsrc@0xCA3557F` |
| `ScanForStatWriteAccess` | `fallback` | 1 | `.rsrc@0xCA3558F` |

## Notes

- `ScanForPlayerPointerCapture` resolves through the `rax-rdx-1.05.01-exe` pattern at match RVA `0x34FB12`; the installed mid-hook target is RVA `0x34FB1A`, and the status marker source is `RDX`.
- The scanner now filters the second pass to executable sections instead of arbitrary non-`.text` sections. This keeps support for this packed/nonstandard section layout while reducing false-positive risk from data sections.
- The exact and relaxed 1.05.01 RDX player-pointer signatures each resolve uniquely.
- `ScanForStatsAccess` has an intentionally broad fallback with 5 matches, but it is not reached for this executable because the primary signature resolves uniquely.
