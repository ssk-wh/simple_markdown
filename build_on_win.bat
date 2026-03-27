@echo off
setlocal
chcp 65001 >nul 2>&1

set BUILD_DIR=build_tools
set BUILD_TYPE=Release

if "%~1"=="debug"   set BUILD_TYPE=Debug
if "%~1"=="release" set BUILD_TYPE=Release
if "%~1"=="clean" (
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
    echo Cleaned.
    exit /b 0
)

echo ================================================
echo   SimpleMarkdown Build [%BUILD_TYPE%]
echo ================================================

set VCVARS=
if exist "D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "D:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="D:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found_vcvars

echo [ERROR] Visual Studio not found.
exit /b 1

:found_vcvars
echo [1/3] Setting up MSVC environment...
call %VCVARS% x64 >nul 2>&1

if not exist %BUILD_DIR%\CMakeCache.txt (
    echo [2/3] CMake configure...
    cmake -S . -B %BUILD_DIR% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
    if errorlevel 1 (
        echo [ERROR] CMake configure failed.
        exit /b 1
    )
) else (
    echo [2/3] CMake cache exists, skipping configure.
)

echo [3/3] Building...
cmake --build %BUILD_DIR%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo ================================================
echo   Build succeeded: %BUILD_DIR%\app\SimpleMarkdown.exe
echo ================================================
