# GitHub upload notes - fix47

Build id: `safe-rva-fixed-fix47-v1001235-compat`

Recommended folder to upload:

`player-status-modifier-v1.0.0.1235-RVA-FIXED-fix47-compat-github`

## Summary

- Compatibility rebuild for the May 11, 2026 game update.
- Verified against `CrimsonDesert.exe` file version `1.0.0.1235`.
- Restores scanner/install compatibility for the updated executable layout.
- Keeps fix46 layered INI behavior and DMM-ready modular config folders.

## Release artifacts

- `release-artifacts/player-status-modifier.asi`
- `release-artifacts/player-status-modifier.ini`
- `release-artifacts/player-status-modifier.default.ini`
- `release-artifacts/module-configs/`
- `release-artifacts/dmm-mods/`
- `release-artifacts/dmm-mods.zip`
- `release-artifacts/SHA256SUMS.txt`

## Verification performed

- Release ASI verifier passed.
- Build marker present in ASI.
- Static compatibility pass found updated fix47 hook sites on the current game executable.
