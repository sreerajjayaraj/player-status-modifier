# Safe Local Build Guide

Use this guide to build the ASI locally instead of downloading a precompiled binary.

## Requirements

- Windows
- Visual Studio 2022 with C++ desktop workload
- CMake support for Visual Studio
- x64 toolchain

## Visual Studio steps

1. Extract this source archive.
2. Open the folder in Visual Studio.
3. Select the CMake configuration:

```text
x64-Release
```

4. Use:

```text
Build → Clean All
Build → Build All
```

5. Copy only the generated file:

```text
player-status-modifier.asi
```

to:

```text
CrimsonDesert\bin64\
```

Do not copy `.pdb`, `.ilk`, `.obj`, `.lib`, old `.dll`, or backup `.asi` files.

## Command-line build

From a Visual Studio Developer PowerShell:

```powershell
.\tools\build-release.ps1
```

This creates:

```text
out\build\x64-Release\player-status-modifier.asi
```

## Verify the ASI

After building:

```powershell
.\tools\verify-release-asi.ps1 -AsiPath .\out\build\x64-Release\player-status-modifier.asi
```

The verifier checks for common mistakes:

- Debug CRT imports such as `MSVCP140D.dll`, `VCRUNTIME140D.dll`, `ucrtbased.dll`
- common network-related API names
- old loader/game executable accidentally included in the local output folder

## Expected runtime safety state

Affinity is intentionally guarded. Until a safe 1.05.01 affinity hook is validated, the log should show:

```text
affinity-prepare=0
affinity-current=0
```

The old partial affinity state is unsafe:

```text
affinity-prepare=0
affinity-current=1
```

Do not use a build that logs that state.
