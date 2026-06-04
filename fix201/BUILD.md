# Build Notes

## Toolchain

- Windows x64
- Visual Studio / MSVC
- CMake

## Commands

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Verified Build Identity

- Version marker: `safe-rva-fixed-109-fix201-itemgain-shadow-guard`
- ASI SHA256: `D3FCB816CA94562B388B2BBA4667E4B0442551D61726D75F3876BCC32D4B140E`
- User ZIP SHA256: `6EBC7F25BEE97F0A0E1B8993BE932561C0368082B26DC4E2F94A5072AC9B3428`

## Deployment Rule

Deploy the ASI through Definitive Mod Manager. Do not directly overwrite the game folder ASI as the normal workflow.

