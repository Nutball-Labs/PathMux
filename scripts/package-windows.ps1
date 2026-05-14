# package-windows.ps1 — Produce ZIP and MSI packages for Windows via CPack and WiX
# Assumes build-windows.ps1 has already been run successfully (includes windeployqt).
#
# Usage:
#   .\scripts\package-windows.ps1
#
# Output: packages\ at project root
#   camclops-X.Y.Z-win64.zip
#   camclops-X.Y.Z-win64.msi
#
# Requires: WiX 6  (dotnet tool install --global wix)
#           https://wixtoolset.org/releases/

$cmake    = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$cpack    = "C:\Qt\Tools\CMake_64\bin\cpack.exe"
$ninja    = "C:\Qt\Tools\Ninja\ninja.exe"
$qtDir    = "C:\Qt\6.10.2\mingw_64"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
$wix      = "$env:USERPROFILE\.dotnet\tools\wix.exe"
$src      = (Split-Path $PSScriptRoot -Parent)
$build    = "C:\tmp\camclops-build-win"

$null = New-Item -ItemType Directory -Force "$src\packages"

# ── Version banner ────────────────────────────────────────────────────────────
$vhp    = Join-Path $src "lib\version.hpp"
$major  = (Select-String '#define VERSION_MAJOR\s+(\d+)'     $vhp).Matches[0].Groups[1].Value
$minor  = (Select-String '#define VERSION_MINOR\s+(\d+)'     $vhp).Matches[0].Groups[1].Value
$patch  = (Select-String '#define VERSION_PATCH\s+(\d+)'     $vhp).Matches[0].Groups[1].Value
$suffix = (Select-String '#define VERSION_SUFFIX\s+"([^"]*)"' $vhp).Matches[0].Groups[1].Value
$version = "${major}.${minor}.${patch}${suffix}"
$inner  = "  Packaging CamClops version $version  --  Windows  "
$border = "*" * ($inner.Length + 2)
Write-Host $border
Write-Host "*${inner}*"
Write-Host $border
Write-Host ""

$cacheFile = Join-Path $build "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cached = (Select-String 'CAMCLOPS_VERSION:STRING=(.+)' $cacheFile -ErrorAction SilentlyContinue)
    $cachedVer = if ($cached) { $cached.Matches[0].Groups[1].Value.Trim() } else { "" }
    if ($cachedVer -ne $version) {
        Write-Host "--- Version changed ($cachedVer -> $version), reconfiguring ---"
        & $cmake -S $src -B $build -G "Ninja" -DCMAKE_BUILD_TYPE=Release `
            -DCMAKE_C_COMPILER="$mingwBin\gcc.exe" `
            -DCMAKE_CXX_COMPILER="$mingwBin\g++.exe" `
            -DCMAKE_MAKE_PROGRAM="$ninja" `
            -DCMAKE_PREFIX_PATH=$qtDir
        if ($LASTEXITCODE -ne 0) { Write-Host "Reconfigure failed"; exit 1 }
        Write-Host ""
    }
}

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
Get-ChildItem "$src\packages\camclops-*-win64.*" |
    Where-Object { $_.Extension -ne ".wixpdb" } |
    Select-Object Name, @{N="Size";E={"{0:N0} KB" -f ($_.Length/1KB)}}

# SN: 00117
