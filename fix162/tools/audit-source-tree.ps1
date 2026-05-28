$ErrorActionPreference = "Stop"

$badExt = @("*.asi","*.dll","*.exe","*.pdb","*.lib","*.obj","*.ilk")
$found = @()

foreach ($pattern in $badExt) {
    $found += Get-ChildItem -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "\\out\\build\\" }
}

if ($found.Count -gt 0) {
    Write-Host "Potential binary files in source tree:" -ForegroundColor Yellow
    $found | ForEach-Object { Write-Host " - $($_.FullName)" }
    exit 1
}

Write-Host "Source tree audit passed: no checked-in binary outputs found outside build folder." -ForegroundColor Green
