# build-windows.ps1 — Full compile for PathMux on Windows (MinGW + Qt6)
# Run directly from PowerShell — locates project root relative to this script.
#
# Usage:
#   .\scripts\build-windows.ps1           -- configure (if needed) + build + Qt deploy
#   .\scripts\build-windows.ps1 -Clean    -- wipe build dir first, then configure + build
#   .\scripts\build-windows.ps1 -NoGui    -- build CLI tools only (skip Qt6 GUI + windeployqt)
#
# Build dir: C:\tmp\pathmux-build-win  (local drive avoids NFS file-locking)
# Requires:  Qt6 Online Installer -> MinGW 13.1 toolchain + Ninja + CMake
#            https://www.qt.io/download-qt-installer
#
# Adjust the tool paths below if your Qt install uses a different version.

param(
    [switch]$Clean,
    [switch]$NoGui
)

$cmake        = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$ninja        = "C:\Qt\Tools\Ninja\ninja.exe"
$qtDir        = "C:\Qt\6.10.2\mingw_64"
$mingwBin     = "C:\Qt\Tools\mingw1310_64\bin"
$windeployqt  = "$qtDir\bin\windeployqt.exe"
$wix          = "$env:USERPROFILE\.dotnet\tools\wix.exe"
$gcc          = "$mingwBin\gcc.exe"
$gpp          = "$mingwBin\g++.exe"
$src          = (Split-Path $PSScriptRoot -Parent)
$build        = "C:\tmp\pathmux-build-win"   # local drive — NFS locks break AutoRcc
$dest         = "N:\pathmux\build-win"       # final artifact destination on NFS

# MinGW must be on PATH so Ninja and the linker can find runtime DLLs
$env:PATH = "$mingwBin;$env:PATH"

if ($Clean -and (Test-Path $build)) {
    Write-Host "--- Cleaning build directory ---"
    Remove-Item -Recurse -Force $build
}

if (-not (Test-Path $build)) {
    Write-Host "--- Configuring (build-win) ---"
    $wixArg = if (Test-Path $wix) { "-DWIX_EXECUTABLE=$wix" } else { $null }
    $cmakeArgs = @(
        "-S", $src,
        "-B", $build,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=$gcc",
        "-DCMAKE_CXX_COMPILER=$gpp",
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_PREFIX_PATH=$qtDir"
    )
    if ($wixArg) { $cmakeArgs += $wixArg }
    & $cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed"; exit 1 }
    Write-Host ""
}

Write-Host "--- Building ---"
& $cmake --build $build
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed"; exit 1 }

# Qt deployment — copies Qt DLLs and plugin directories into the build dir so
# package-windows.ps1 can harvest them for the ZIP and MSI.
if (-not $NoGui) {
    if (Test-Path $windeployqt) {
        Write-Host ""
        Write-Host "--- Qt deployment (windeployqt) ---"
        & $windeployqt --release --no-translations "$build\pathmux-gui.exe"
        if ($LASTEXITCODE -ne 0) { Write-Host "windeployqt (pathmux-gui) failed"; exit 1 }
        if (Test-Path "$build\pathmux-tl.exe") {
            & $windeployqt --release --no-translations "$build\pathmux-tl.exe"
            if ($LASTEXITCODE -ne 0) { Write-Host "windeployqt (pathmux-tl) failed"; exit 1 }
        }
    } else {
        Write-Host "WARNING: windeployqt not found at $windeployqt - Qt DLLs will not be bundled"
        Write-Host "         Adjust `$windeployqt in build-windows.ps1 or install Qt via the Qt Online Installer"
    }
}

Write-Host ""
Write-Host "--- Copying artifacts to $dest ---"
$null = New-Item -ItemType Directory -Force $dest
Get-ChildItem -Path $build -Filter "*.exe" | Copy-Item -Destination $dest -Force
Get-ChildItem -Path $build -Filter "*.dll" | Copy-Item -Destination $dest -Force
Write-Host "Done. Artifacts in ${dest}"

# SN: 00106
