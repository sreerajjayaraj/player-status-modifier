param(
    [Parameter(Mandatory=$true)]
    [string]$AsiPath
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $AsiPath)) {
    throw "ASI not found: $AsiPath"
}

$bytes = [System.IO.File]::ReadAllBytes($AsiPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

$debugCrt = @(
    "MSVCP140D.dll",
    "VCRUNTIME140D.dll",
    "VCRUNTIME140_1D.dll",
    "ucrtbased.dll"
)

$networkNames = @(
    "WINHTTP.dll",
    "WININET.dll",
    "WS2_32.dll",
    "WinHttp",
    "InternetOpen",
    "URLDownload",
    "WSAStartup",
    "socket",
    "connect"
)

$bad = @()

foreach ($name in $debugCrt) {
    if ($text.Contains($name)) {
        $bad += "Debug CRT reference found: $name"
    }
}

foreach ($name in $networkNames) {
    if ($text.Contains($name)) {
        $bad += "Network-related reference found: $name"
    }
}

Write-Host "ASI: $AsiPath"
Get-FileHash $AsiPath -Algorithm SHA256 | Format-List

if ($bad.Count -gt 0) {
    Write-Host ""
    Write-Host "Verification failed:" -ForegroundColor Red
    foreach ($item in $bad) {
        Write-Host " - $item" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Verification passed: no debug CRT or common network references found." -ForegroundColor Green
