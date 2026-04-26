# build-windows.ps1 — Full compile for PathMux on Windows (MinGW + Qt6)
# Run directly from PowerShell — locates project root relative to this script.
#
# Usage:
#   .\scripts\build-windows.ps1           -- configure (if needed) + build
#   .\scripts\build-windows.ps1 -Clean    -- wipe build dir first, then configure + build
#   .\scripts\build-windows.ps1 -NoGui    -- build CLI tools only (skip Qt6 GUI)
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

$cmake     = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$ninja     = "C:\Qt\Tools\Ninja\ninja.exe"
$qtDir     = "C:\Qt\6.10.2\mingw_64"
$mingwBin  = "C:\Qt\Tools\mingw1310_64\bin"
$gcc       = "$mingwBin\gcc.exe"
$gpp       = "$mingwBin\g++.exe"
$src       = (Split-Path $PSScriptRoot -Parent)
$build     = "C:\tmp\pathmux-build-win"   # local drive — NFS locks break AutoRcc
$dest      = "N:\pathmux\build-win"       # final artifact destination on NFS

# MinGW must be on PATH so Ninja and the linker can find runtime DLLs
$env:PATH = "$mingwBin;$env:PATH"

if ($Clean -and (Test-Path $build)) {
    Write-Host "--- Cleaning build directory ---"
    Remove-Item -Recurse -Force $build
}

if (-not (Test-Path $build)) {
    Write-Host "--- Configuring (build-win) ---"
    $args = @(
        "-S", $src,
        "-B", $build,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=$gcc",
        "-DCMAKE_CXX_COMPILER=$gpp",
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_PREFIX_PATH=$qtDir"
    )
    & $cmake @args
    if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed"; exit 1 }
    Write-Host ""
}

Write-Host "--- Building ---"
& $cmake --build $build
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed"; exit 1 }

Write-Host ""
Write-Host "--- Copying artifacts to $dest ---"
$null = New-Item -ItemType Directory -Force $dest
Get-ChildItem -Path $build -Filter "*.exe" | Copy-Item -Destination $dest -Force
Get-ChildItem -Path $build -Filter "*.dll" | Copy-Item -Destination $dest -Force
Write-Host "Done. Artifacts in $dest\"

# SN: 00095
