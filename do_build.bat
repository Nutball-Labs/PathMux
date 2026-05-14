@echo off
set SRC=%~dp0
set BLD=C:\tmp\camclops-build-win
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat

call "%VCVARS%" > NUL 2>&1

"%CMAKE%" -S "%SRC%" -B "%BLD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BLD%"

REM SN: 00097
