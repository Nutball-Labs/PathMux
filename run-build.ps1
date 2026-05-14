# CamClops Windows build + package — thin wrapper
# Usage:
#   .\run-build.ps1           — build (including Qt deploy) + ZIP + MSI
#   .\run-build.ps1 -NoPack   — build only
#   .\run-build.ps1 -NoGui    — CLI tools only (no windeployqt, no Qt DLLs)
#   .\run-build.ps1 -Clean    — wipe build dir first
param(
    [switch]$NoPack,
    [switch]$NoGui,
    [switch]$Clean
)

& "$PSScriptRoot\scripts\build-windows.ps1" -NoGui:$NoGui -Clean:$Clean
if ($LASTEXITCODE -ne 0) { exit 1 }

if (-not $NoPack) {
    & "$PSScriptRoot\scripts\package-windows.ps1"
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

# SN: 00106
