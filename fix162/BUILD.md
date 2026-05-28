# Build

## Requirements

- Windows
- Visual Studio 2026 or compatible MSVC x64 toolchain
- CMake
- Node.js for the release post-processing script

## Build Command

From the repository root:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cmake --build "out\build\x64-Release-fix58-economy" --config Release --target player-status-modifier
node tools\strip-empty-tls-directory.js out\build\x64-Release-fix58-economy\player-status-modifier.asi
```

The build directory name is historical. The current full build target is `player-status-modifier`.

## Verification

Check the ASI version metadata:

```powershell
(Get-Item .\player-status-modifier.asi).VersionInfo
```

Check that the PE Thread Storage Directory is absent:

```bat
dumpbin /headers player-status-modifier.asi
```

The `Thread Storage Directory` line should show:

```text
0 [       0] RVA [size] of Thread Storage Directory
```
