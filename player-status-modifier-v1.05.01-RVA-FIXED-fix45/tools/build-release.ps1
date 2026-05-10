param(
    [string]$BuildDir = "out\build\x64-Release",
    [string]$ZydisSourceDir = $env:PLAYER_STATUS_MODIFIER_ZYDIS_DIR,
    [string]$ZycoreSourceDir = $env:PLAYER_STATUS_MODIFIER_ZYCORE_DIR
)

$ErrorActionPreference = "Stop"

Write-Host "Configuring x64 Release build..."
$configureArgs = @("-S", ".", "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
if ($ZydisSourceDir) {
    $resolvedZydis = (Resolve-Path -LiteralPath $ZydisSourceDir).Path
    $zydisCmake = Join-Path $resolvedZydis "CMakeLists.txt"
    if (!(Test-Path -LiteralPath $zydisCmake)) {
        throw "ZydisSourceDir does not contain CMakeLists.txt: $resolvedZydis"
    }
    $configureArgs += "-DPLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR=$resolvedZydis"
    Write-Host "Using local Zydis source: $resolvedZydis"
}
if ($ZycoreSourceDir) {
    $resolvedZycore = (Resolve-Path -LiteralPath $ZycoreSourceDir).Path
    $zycoreCmake = Join-Path $resolvedZycore "CMakeLists.txt"
    if (!(Test-Path -LiteralPath $zycoreCmake)) {
        throw "ZycoreSourceDir does not contain CMakeLists.txt: $resolvedZycore"
    }
    $configureArgs += "-DPLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR=$resolvedZycore"
    Write-Host "Using local Zycore source: $resolvedZycore"
}
cmake @configureArgs

Write-Host "Building..."
cmake --build $BuildDir --config Release

$asi = Join-Path $BuildDir "player-status-modifier.asi"
if (!(Test-Path $asi)) {
    throw "Build completed but $asi was not found."
}

Write-Host "Built: $asi"
Get-FileHash $asi -Algorithm SHA256 | Format-List

Write-Host "Run tools\verify-release-asi.ps1 before copying to the game folder."
