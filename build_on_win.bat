@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

set BUILD_DIR=build
set BUILD_TYPE=Release
set EXPECTED_GENERATOR=NMake Makefiles
set FORCE_CLEAN=0

REM ---- Parse arguments ----
set CMAKE_OPTS=

REM First argument: debug/release/clean
if "%~1"=="debug"   set BUILD_TYPE=Debug
if "%~1"=="release" set BUILD_TYPE=Release
if "%~1"=="clean" (
    if exist %BUILD_DIR% (
        echo Cleaning build directory...
        rmdir /s /q %BUILD_DIR%
    )
    echo Cleaned.
    exit /b 0
)
if "%~1"=="--clean" set FORCE_CLEAN=1

REM Check if second argument is "--" for cmake options
if "%~2"=="--" (
    setlocal enabledelayedexpansion
    if not "%~3"=="" set "CMAKE_OPTS=%~3"
    if not "%~4"=="" set "CMAKE_OPTS=!CMAKE_OPTS! %~4"
    if not "%~5"=="" set "CMAKE_OPTS=!CMAKE_OPTS! %~5"
    if not "%~6"=="" set "CMAKE_OPTS=!CMAKE_OPTS! %~6"
    if not "%~7"=="" set "CMAKE_OPTS=!CMAKE_OPTS! %~7"
    if not "%~8"=="" set "CMAKE_OPTS=!CMAKE_OPTS! %~8"
)

echo ================================================
echo   SimpleMarkdown Build [%BUILD_TYPE%]
echo ================================================
echo.

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
if errorlevel 1 (
    echo [ERROR] Failed to set up MSVC environment.
    exit /b 1
)

REM ---- Find Qt5 (prefer 5.12.9) ----
set QT_DIR=
if defined Qt5_DIR set "QT_DIR=%Qt5_DIR%"
if defined QT_DIR goto qt_found
for %%D in (C D E) do (
    if not defined QT_DIR if exist "%%D:\Qt\Qt5.12.9\5.12.9\msvc2017_64\bin\Qt5Core.dll" set "QT_DIR=%%D:\Qt\Qt5.12.9\5.12.9\msvc2017_64"
)
if defined QT_DIR goto qt_found
for %%D in (C D E) do (
    for /d %%Q in ("%%D:\Qt\Qt5*") do (
        if not defined QT_DIR for /d %%V in ("%%Q\5*") do (
            if not defined QT_DIR for /d %%A in ("%%V\msvc*_64") do set "QT_DIR=%%A"
        )
    )
    for /d %%V in ("%%D:\Qt\5*") do (
        if not defined QT_DIR for /d %%A in ("%%V\msvc*_64") do set "QT_DIR=%%A"
    )
)
:qt_found
if defined QT_DIR (
    set "QT_CMAKE_OPT=-DCMAKE_PREFIX_PATH=%QT_DIR%"
    echo       Qt5: %QT_DIR%
) else (
    set QT_CMAKE_OPT=
)
echo.

REM ---- CMake configure ----
echo [2/3] CMake configure...
if "%FORCE_CLEAN%"=="1" (
    if exist %BUILD_DIR%\CMakeCache.txt del /q %BUILD_DIR%\CMakeCache.txt >nul 2>&1
    if exist %BUILD_DIR%\CMakeFiles rmdir /s /q %BUILD_DIR%\CMakeFiles >nul 2>&1
)
cmake -S . -B %BUILD_DIR% -G "%EXPECTED_GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %QT_CMAKE_OPT% %CMAKE_OPTS%
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)
echo.

REM ---- Build ----
echo [3/3] Building...
cmake --build %BUILD_DIR%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)
echo.

REM ---- Deploy Qt runtime next to exe (so we can launch from build dir) ----
REM 首次构建或缺依赖时会拷贝 Qt DLL + platforms\qwindows.dll；已存在则 windeployqt 自行跳过。
REM 不拷贝依赖直接运行会以 0xC0000135 (STATUS_DLL_NOT_FOUND) 秒退。
if defined QT_DIR (
    if exist "%QT_DIR%\bin\windeployqt.exe" (
        if exist %BUILD_DIR%\src\app\SimpleMarkdown.exe (
            if not exist %BUILD_DIR%\src\app\platforms\qwindows.dll (
                echo [+] Deploying Qt runtime...
                "%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import %BUILD_DIR%\src\app\SimpleMarkdown.exe >nul 2>&1
            )
        )
    )
)

REM ---- Mirror Qt runtime to build\tests for ctest ----
REM 测试 exe 在 build\tests\ 下；它们也需要 Qt DLL 才能启动（否则 ctest 报 0xC000007B
REM = STATUS_INVALID_IMAGE_FORMAT，本质是 DLL 缺失）。从 build\src\app\ 同步即可，
REM 加上 Qt5Test.dll（SimpleMarkdown 本身不依赖 Qt5::Test，但部分测试用 QSignalSpy 依赖它）。
if exist %BUILD_DIR%\tests (
    if exist %BUILD_DIR%\src\app\Qt5Core.dll (
        echo [+] Mirroring Qt runtime to %BUILD_DIR%\tests...
        for %%F in (%BUILD_DIR%\src\app\Qt5*.dll) do (
            if not exist %BUILD_DIR%\tests\%%~nxF copy /y %%F %BUILD_DIR%\tests\ >nul 2>&1
        )
        if exist %BUILD_DIR%\src\app\platforms (
            if not exist %BUILD_DIR%\tests\platforms xcopy /e /i /y %BUILD_DIR%\src\app\platforms %BUILD_DIR%\tests\platforms >nul 2>&1
        )
        if defined QT_DIR (
            if exist "%QT_DIR%\bin\Qt5Test.dll" (
                if not exist %BUILD_DIR%\tests\Qt5Test.dll copy /y "%QT_DIR%\bin\Qt5Test.dll" %BUILD_DIR%\tests\ >nul 2>&1
            )
        )
    )
)

REM ---- Verify output ----
if exist %BUILD_DIR%\src\app\SimpleMarkdown.exe (
    for %%A in (%BUILD_DIR%\src\app\SimpleMarkdown.exe) do set EXE_SIZE=%%~zA
    echo ================================================
    echo   Build succeeded: %BUILD_DIR%\src\app\SimpleMarkdown.exe
    echo   Size: !EXE_SIZE! bytes
    echo ================================================
    exit /b 0
) else (
    echo [ERROR] Build completed but executable not found!
    echo         Expected: %BUILD_DIR%\src\app\SimpleMarkdown.exe
    exit /b 1
)
