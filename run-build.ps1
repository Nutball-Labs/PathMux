# PathMux Windows build + package — PowerShell runner
# Usage:
#   .\run-build.ps1           — configure, build, ZIP, MSI
#   .\run-build.ps1 -NoPack   — configure + build only
param(
    [switch]$NoPack,
    [switch]$NoGui       # skip windeployqt + GUI MSI step (CLI-only build)
)

$vcvars       = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake        = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$wix          = "$env:USERPROFILE\.dotnet\tools\wix.exe"
$windeployqt  = "C:\Qt\6.6.2\msvc2019_64\bin\windeployqt.exe"   # adjust Qt version path if needed
$src          = "C:\Users\iceberg\Nutball-Labs\pathmux"
$build        = "C:\Users\iceberg\Nutball-Labs\pathmux\build-win"
$out          = "C:\Users\iceberg\Nutball-Labs\pathmux\build_out.txt"

# Source vcvars and capture the resulting environment
$envBlock = cmd /c "`"$vcvars`" > NUL 2>&1 && set" 2>$null
foreach ($line in $envBlock) {
    if ($line -match "^([^=]+)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
    }
}

# Ensure packages/ dir exists
$null = New-Item -ItemType Directory -Force "$src\packages"

# Configure — pass wix.exe path explicitly so find_program() doesn't depend on PATH
$wixArg = if (Test-Path $wix) { "-DWIX_EXECUTABLE=$wix" } else { "" }
& $cmake -S $src -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl $wixArg 2>&1 | Tee-Object -FilePath $out
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

# SN: 00092
