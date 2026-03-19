# PathMux Windows build — PowerShell runner
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
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

# Configure
& $cmake -S $src -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl 2>&1 | Tee-Object -FilePath $out

# Build
& $cmake --build $build 2>&1 | Tee-Object -FilePath $out -Append

Write-Host "`nDone. Exit: $LASTEXITCODE"

# SN: 00087
