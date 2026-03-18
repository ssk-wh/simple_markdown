@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

echo ========================================
echo  SimpleMarkdown Installer Build Script
echo ========================================

:: Configuration
set "PROJECT_DIR=%~dp0.."
set "BUILD_DIR=%PROJECT_DIR%\build"
set "DIST_DIR=%~dp0dist"
set "QT_DIR="

:: Find Qt installation (search D:\Qt\5.15.x\msvc2019_64 and C:\Qt\5.15.x\msvc2019_64)
for /d %%v in ("D:\Qt\5.15*" "C:\Qt\5.15*") do (
    if exist "%%v\msvc2019_64\bin\Qt5Core.dll" set "QT_DIR=%%v\msvc2019_64"
)
:: Also check flat layout (D:\Qt\5.15.2\msvc2019_64 already matched above)
:: And try Qt5_DIR env var if set
if "%QT_DIR%"=="" if defined Qt5_DIR (
    for %%d in ("%Qt5_DIR%\..\..\..") do if exist "%%~fd\bin\Qt5Core.dll" set "QT_DIR=%%~fd"
)

if "%QT_DIR%"=="" (
    echo [ERROR] Qt 5.15 msvc2019_64 not found
    exit /b 1
)
echo [INFO] Qt found: %QT_DIR%

:: Extract version from CMakeLists.txt
for /f "tokens=3" %%v in ('findstr /r "project(SimpleMarkdown VERSION" "%PROJECT_DIR%\CMakeLists.txt"') do (
    set "VERSION=%%v"
)
:: Clean trailing parenthesis
set "VERSION=%VERSION:)=%"
if "%VERSION%"=="" set "VERSION=0.1.0"
echo [INFO] Version: %VERSION%

:: Build Release
echo [STEP 1] Building Release...
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" 2>nul
cmake --build "%BUILD_DIR%" --config Release --target SimpleMarkdown
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

:: Collect dist files
echo [STEP 2] Collecting files...
if exist "%DIST_DIR%" rd /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\platforms"
mkdir "%DIST_DIR%\imageformats"
mkdir "%DIST_DIR%\styles"

:: Copy exe
copy "%BUILD_DIR%\app\Release\SimpleMarkdown.exe" "%DIST_DIR%\" >nul

:: Copy Qt DLLs
for %%f in (Qt5Core Qt5Gui Qt5Widgets Qt5Network Qt5Svg) do (
    if exist "%QT_DIR%\bin\%%f.dll" copy "%QT_DIR%\bin\%%f.dll" "%DIST_DIR%\" >nul
)

:: Copy ANGLE/OpenGL DLLs
for %%f in (libEGL libGLESv2 D3Dcompiler_47 opengl32sw) do (
    if exist "%QT_DIR%\bin\%%f.dll" copy "%QT_DIR%\bin\%%f.dll" "%DIST_DIR%\" >nul
)

:: Copy Qt plugins
copy "%QT_DIR%\plugins\platforms\qwindows.dll" "%DIST_DIR%\platforms\" >nul
for %%f in (qjpeg qgif qico qsvg qwebp qtiff) do (
    if exist "%QT_DIR%\plugins\imageformats\%%f.dll" copy "%QT_DIR%\plugins\imageformats\%%f.dll" "%DIST_DIR%\imageformats\" >nul
)
if exist "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" (
    copy "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" "%DIST_DIR%\styles\" >nul
)

:: Copy MSVC runtime (if not system-wide)
for %%f in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
    if exist "%QT_DIR%\bin\%%f" copy "%QT_DIR%\bin\%%f" "%DIST_DIR%\" >nul
)

:: Count files
set /a count=0
for /r "%DIST_DIR%" %%f in (*) do set /a count+=1
echo [INFO] Collected %count% files

:: Update version in NSIS script
echo [STEP 3] Building installer...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0EasyMarkdown.nsi') -replace '!define APP_VERSION \".*\"', '!define APP_VERSION \"%VERSION%\"' | Set-Content -Encoding UTF8 '%~dp0EasyMarkdown.nsi'"


:: Build NSIS installer
where makensis >nul 2>&1
if errorlevel 1 (
    echo [WARN] makensis not found, skipping installer creation
    echo [INFO] Portable files are in: %DIST_DIR%
    goto :done
)

cd /d "%~dp0"
makensis EasyMarkdown.nsi
if errorlevel 1 (
    echo [ERROR] NSIS build failed
    exit /b 1
)

:done
echo ========================================
echo  Build complete!
echo  Portable: %DIST_DIR%
if exist "%~dp0SimpleMarkdown-%VERSION%-Setup.exe" echo  Installer: %~dp0SimpleMarkdown-%VERSION%-Setup.exe
echo ========================================
