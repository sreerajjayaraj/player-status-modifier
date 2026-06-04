# Crimson Desert Player Status Modifier

Version: `1.09-fix201-itemgain-shadow-guard`

Player Status Modifier is an ASI plugin for Crimson Desert that scales player-facing status systems at runtime through an INI file.

## Features

- Outgoing damage multiplier.
- Incoming damage multiplier for playable characters.
- Health consumption and healing multipliers.
- Stamina consumption and recovery multipliers.
- Spirit / MP resource consumption and recovery multipliers.
- Item gain multiplier.
- Mount and special mount stamina lock.
- Live INI reload for gameplay tuning without rebuilding.
- Process guard so the ASI exits unless loaded by `CrimsonDesert.exe`.

## Fix201 Notes

This build keeps the verified fix200 playable-character resource behavior and adds a guard for duplicate item-gain calls seen during interaction and transition windows.

Validated behavior in local testing:

- Kliff, Damiane, and Oongka retain health, stamina, and spirit handling across normal gameplay.
- Boss rematch checks no longer showed the previously reported stamina/spirit regression in this verified run.
- Item gain multiplier remains active for normal inventory-style gain paths.
- Duplicate or shadow item-gain calls are skipped instead of scaled.
- No repeat of the previously reported gameplay issues was seen in the fix201 test log.

## Installation

Use Definitive Mod Manager for the ASI:

1. Place `player-status-modifier.asi` in the DMM mods folder.
2. Keep `player-status-modifier.ini` beside the ASI for DMM config handling.
3. Let DMM deploy the ASI into the game folder.
4. If you are live testing custom values, the active INI is the one in `bin64`.

Do not manually install the ASI into the game folder unless you are deliberately bypassing DMM.

## Build

This project builds with Visual Studio / MSVC CMake on Windows x64.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The built ASI is emitted as `player-status-modifier.asi`.

## Release Package

The included release package for users is:

`release/PSMv109.201.zip`

It contains only:

- `player-status-modifier.asi`
- `player-status-modifier.ini`

