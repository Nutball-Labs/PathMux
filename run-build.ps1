# PathMux Windows build + package — PowerShell runner
# Usage:
#   .\run-build.ps1           — configure, build, ZIP, MSI
#   .\run-build.ps1 -NoPack   — configure + build only
param(
    [switch]$NoPack
)

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$wix    = "$env:USERPROFILE\.dotnet\tools\wix.exe"
$src    = "C:\Users\iceberg\Nutball-Labs\pathmux"
$build  = "C:\Users\iceberg\Nutball-Labs\pathmux\build-win"
$out    = "C:\Users\iceberg\Nutball-Labs\pathmux\build_out.txt"

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

# SN: 00090
