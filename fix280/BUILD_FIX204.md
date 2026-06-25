# Build Notes - fix204

Use a Visual Studio x64 developer shell.

```powershell
cmake -S . -B build-fix204 -G "Visual Studio 18 2026" -A x64
cmake --build build-fix204 --config Release --target player-status-modifier
```

The verified deployment artifact is included in `dist/player-status-modifier.asi`.

Do not deploy by direct ASI copy to bin64. Stage the ASI in DMM mods and let DMM redeploy it.
