# PathMux Windows build + package — PowerShell runner
# Usage:
#   .\run-build.ps1           — configure, build, ZIP, MSI
#   .\run-build.ps1 -NoPack   — configure + build only
param(
    [switch]$NoPack,
    [switch]$NoGui       # skip windeployqt + GUI MSI step (CLI-only build)
)

$cmake        = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$ninja        = "C:\Qt\Tools\Ninja\ninja.exe"
$wix          = "$env:USERPROFILE\.dotnet\tools\wix.exe"
$qtDir        = "C:\Qt\6.10.2\mingw_64"
$mingwBin     = "C:\Qt\Tools\mingw1310_64\bin"
$windeployqt  = "$qtDir\bin\windeployqt.exe"
$gcc          = "$mingwBin\gcc.exe"
$gpp          = "$mingwBin\g++.exe"
$src          = "C:\Users\iceberg\Nutball-Labs\pathmux"
$build        = "C:\Users\iceberg\Nutball-Labs\pathmux\build-win"
$out          = "C:\Users\iceberg\Nutball-Labs\pathmux\build_out.txt"

# Add MinGW to PATH so Ninja can find gcc/g++ and MinGW runtime DLLs
$env:PATH = "$mingwBin;$env:PATH"

# Ensure packages/ dir exists
$null = New-Item -ItemType Directory -Force "$src\packages"

# Configure — MinGW + Qt 6.10.2; pass wix.exe path explicitly so find_program() doesn't depend on PATH
$wixArg = if (Test-Path $wix) { "-DWIX_EXECUTABLE=$wix" } else { "" }
& $cmake -S $src -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER="$gcc" -DCMAKE_CXX_COMPILER="$gpp" `
    -DCMAKE_MAKE_PROGRAM="$ninja" `
    -DCMAKE_PREFIX_PATH="$qtDir" `
    $wixArg 2>&1 | Tee-Object -FilePath $out
if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed"; exit 1 }

# Build
& $cmake --build $build 2>&1 | Tee-Object -FilePath $out -Append
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed"; exit 1 }

# Qt deployment — copies Qt DLLs, platform plugins, and QML files next to
# pathmux-gui.exe so it runs without a separate Qt installation.
# Must run before CPack and MSI steps; CPack ZIP captures the result.
if (-not $NoGui) {
    if (Test-Path $windeployqt) {
        Write-Host "`n--- Qt deployment (windeployqt) ---"
        & $windeployqt --release --no-translations "$build\pathmux-gui.exe" 2>&1 | Tee-Object -FilePath $out -Append
        if ($LASTEXITCODE -ne 0) { Write-Host "windeployqt failed"; exit 1 }
    } else {
        Write-Host "WARNING: windeployqt not found at $windeployqt — GUI may not run without Qt installed"
        Write-Host "         Adjust `$windeployqt path in run-build.ps1 or install Qt via the Qt Online Installer"
    }
}

if (-not $NoPack) {
    # ZIP via CPack
    Write-Host "`n--- Packaging ZIP ---"
    & $cmake --build $build --target package 2>&1 | Tee-Object -FilePath $out -Append
    if ($LASTEXITCODE -ne 0) { Write-Host "CPack (ZIP) failed"; exit 1 }

    # MSI via WiX 6 custom target
    Write-Host "`n--- Packaging MSI ---"
    & $cmake --build $build --target msi 2>&1 | Tee-Object -FilePath $out -Append
    if ($LASTEXITCODE -ne 0) { Write-Host "MSI build failed (is wix.exe in PATH?)"; exit 1 }

    Write-Host "`nPackages:"
    Get-ChildItem "$src\packages\pathmux-*-win64.*" | Select-Object Name, Length
}

Write-Host "`nDone. Exit: $LASTEXITCODE"

# SN: 00094
