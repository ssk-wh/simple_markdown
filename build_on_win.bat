@echo off
setlocal
chcp 65001 >nul 2>&1

set BUILD_DIR=build
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

REM ---- Find Visual Studio ----
set VCVARS=
for %%D in (C D) do (
    for %%V in (2022 2019) do (
        for %%E in (Community Professional Enterprise) do (
            if not defined VCVARS (
                if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
                    set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat"
                )
                if exist "%%D:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
                    set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat"
                )
            )
        )
    )
)
if not defined VCVARS (
    echo [ERROR] Visual Studio not found.
    exit /b 1
)

echo [1/3] Setting up MSVC environment...
call "%VCVARS%" x64 >nul 2>&1

REM ---- Find Qt5 ----
set QT_CMAKE_OPT=
if defined Qt5_DIR (
    echo       Qt5: %Qt5_DIR%
) else (
    for %%D in (C D) do (
        for /d %%Q in ("%%D:\Qt\Qt5*" "%%D:\Qt\5*") do (
            if not defined QT_CMAKE_OPT (
                for /d %%A in ("%%Q\*msvc*_64") do (
                    set "QT_CMAKE_OPT=-DCMAKE_PREFIX_PATH=%%A"
                    echo       Qt5: %%A
                )
            )
        )
    )
)

REM ---- Configure ----
if not exist %BUILD_DIR%\CMakeCache.txt (
    echo [2/3] CMake configure...
    cmake -S . -B %BUILD_DIR% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %QT_CMAKE_OPT%
    if errorlevel 1 (
        echo [ERROR] CMake configure failed.
        echo         Make sure Qt5 is installed. Set Qt5_DIR or CMAKE_PREFIX_PATH if needed.
        exit /b 1
    )
) else (
    echo [2/3] CMake cache exists, skipping configure.
)

REM ---- Build ----
echo [3/3] Building...
cmake --build %BUILD_DIR%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo ================================================
echo   Build succeeded: %BUILD_DIR%\app\SimpleMarkdown.exe
echo ================================================
