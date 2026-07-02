# Player Status Modifier d100

Build: fix208-d100-1.0.0.1855-primary-spirit-player-mount-guard
Game target: Crimson Desert 1.12.01
Date: 2026-07-01

## Reason

d99 still allowed Damiane/Oongka player stamina blocks to overlap with trusted mount state. Logs showed player-looking stamina entries being routed as mount stamina, stale player stamina entries remaining active, and player promotion sometimes resolving through the legacy spirit slot at health + 0x1B0 instead of the current primary spirit slot at health + 0x630.

## Changes

- Player combat block resolution now requires primary Spirit type 20 at health + 0x630.
- Legacy spirit at health + 0x1B0 is no longer accepted for active player combat promotion.
- Player promotion clears stale trusted-mount overlap instead of rejecting the corrected player block.
- Mount profile detection rejects full player-shaped health/stamina/spirit blocks so Damiane/Oongka are not treated as ground mounts.

## Evidence

Failing recording: C:\Users\sreer\Videos\2026-07-01 20-18-05.mkv
Recording SHA256: AF0A49B53630DDAE0920FF9F9910EE6F340E3FF7ED0B4CB4146C008E073146D1
Recording: HEVC 2560x1440 60 fps, duration 298.317 s

Built ASI SHA256: A5048F610802A04D0DA983FFDD6B1374F2BCB2AE185AABF7C76E0BC74ED46CF3
DMM deployed ASI SHA256: A5048F610802A04D0DA983FFDD6B1374F2BCB2AE185AABF7C76E0BC74ED46CF3
Previous DMM backup: E:\CD_Mods_Master\backups\player-status-modifier\pre-d100-20260701\player-status-modifier.pre-d100.asi
Previous DMM backup SHA256: 1DD6A8480B2D7335A528476A375B42420FE25A65F402ADEE76341BD50A73E6F4

## Deployment

Copied d100 only to DMM mods:
E:\Downloads\Compressed\CD_Mods\DMM\mods\player-status-modifier.asi

bin64 was not directly overwritten. DMM must redeploy the ASI to bin64.

## Verification

- Build succeeded with MSVC / Visual Studio 2026.
- Empty TLS directory stripped.
- dumpbin shows Thread Storage Directory as 0 RVA / 0 size.
