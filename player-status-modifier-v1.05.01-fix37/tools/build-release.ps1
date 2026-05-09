param(
    [string]$BuildDir = "out\build\x64-Release"
)

$ErrorActionPreference = "Stop"

Write-Host "Configuring x64 Release build..."
cmake -S . -B $BuildDir -G "Ninja" -DCMAKE_BUILD_TYPE=Release

Write-Host "Building..."
cmake --build $BuildDir --config Release

$asi = Join-Path $BuildDir "player-status-modifier.asi"
if (!(Test-Path $asi)) {
    throw "Build completed but $asi was not found."
}

Write-Host "Built: $asi"
Get-FileHash $asi -Algorithm SHA256 | Format-List

Write-Host "Run tools\verify-release-asi.ps1 before copying to the game folder."
