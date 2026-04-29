# package-windows.ps1 — Produce ZIP and MSI packages for Windows via CPack and WiX
# Assumes build-windows.ps1 has already been run successfully (includes windeployqt).
#
# Usage:
#   .\scripts\package-windows.ps1
#
# Output: packages\ at project root
#   pathmux-X.Y.Z-win64.zip
#   pathmux-X.Y.Z-win64.msi
#
# Requires: WiX 6  (dotnet tool install --global wix)
#           https://wixtoolset.org/releases/

$cmake  = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$cpack  = "C:\Qt\Tools\CMake_64\bin\cpack.exe"
$wix    = "$env:USERPROFILE\.dotnet\tools\wix.exe"
$src    = (Split-Path $PSScriptRoot -Parent)
$build  = "C:\tmp\pathmux-build-win"

$null = New-Item -ItemType Directory -Force "$src\packages"

# ---------------------------------------------------------------------------
# CPack ZIP
# ---------------------------------------------------------------------------
Write-Host "=== Package ZIP (CPack) ==="
Push-Location $build
& $cpack
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { Write-Host "CPack (ZIP) failed"; exit 1 }

# ---------------------------------------------------------------------------
# WiX MSI
# ---------------------------------------------------------------------------
if (Test-Path $wix) {
    Write-Host ""
    Write-Host "=== Package MSI (WiX 6) ==="
    & $cmake --build $build --target msi
    if ($LASTEXITCODE -ne 0) { Write-Host "MSI build failed"; exit 1 }
} else {
    Write-Host ""
    Write-Host "WARNING: wix.exe not found at $wix -- MSI skipped."
    Write-Host "         Install: dotnet tool install --global wix"
}

Write-Host ""
Write-Host "Packages:"
Get-ChildItem "$src\packages\pathmux-*-win64.*" |
    Where-Object { $_.Extension -ne ".wixpdb" } |
    Select-Object Name, @{N="Size";E={"{0:N0} KB" -f ($_.Length/1KB)}}

# SN: 00106
