@echo off
setlocal EnableDelayedExpansion

title G29 FFB Mod - Build

echo =====================================================
echo  G29 FFB Mod for NFS Underground 2 - Build Script
echo =====================================================
echo.

:: ---------- Locate Visual Studio via vswhere ----------
:: Note: %ProgramFiles(x86)% has parens - must assign to a plain var first
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found at:
    echo   %VSWHERE%
    echo.
    echo Install Visual Studio Community (free):
    echo   https://visualstudio.microsoft.com/downloads/
    echo   Required workload: "Desktop development with C++"
    pause
    exit /b 1
)

:: Get the latest VS installation path
:: Note: vswhere path contains (x86) — backtick expansion would misparse the )
:: Redirect to temp file to avoid that issue entirely.
"%VSWHERE%" -latest -property installationPath > "%TEMP%\g29_vs_path.txt" 2>nul
for /f "usebackq tokens=*" %%i in ("%TEMP%\g29_vs_path.txt") do set "VS_PATH=%%i"
del "%TEMP%\g29_vs_path.txt" >nul 2>&1

if not defined VS_PATH (
    echo [ERROR] No Visual Studio installation found by vswhere.
    echo Install the "Desktop development with C++" workload.
    pause
    exit /b 1
)

set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars32.bat"

if not exist "%VCVARS%" (
    echo [ERROR] vcvars32.bat not found at:
    echo   %VCVARS%
    echo.
    echo Make sure the "Desktop development with C++" workload is installed.
    pause
    exit /b 1
)

echo [OK] Visual Studio found at: %VS_PATH%

:: ---------- Locate CMake (bundled with VS first) ----------
set "CMAKE_BUNDLED=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

where cmake >nul 2>&1
if not errorlevel 1 goto :cmake_found

if exist "%CMAKE_BUNDLED%\cmake.exe" (
    set "PATH=%PATH%;%CMAKE_BUNDLED%"
    goto :cmake_found
)

if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set "PATH=%PATH%;C:\Program Files\CMake\bin"
    goto :cmake_found
)

echo [ERROR] CMake not found.
echo.
echo Options:
echo   1. Via Visual Studio Installer: add "C++ CMake tools for Windows"
echo   2. Standalone: https://cmake.org/download/
pause
exit /b 1

:cmake_found
echo [OK] CMake found

:: ---------- Init MSVC x86 environment ----------
echo.
echo [SETUP] Initializing MSVC x86 toolchain...
call "%VCVARS%"
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio environment.
    pause
    exit /b 1
)

:: ---------- Configure project ----------
set "SOURCE_DIR=%~dp0"
set "BUILD_DIR=%~dp0build_output"
set "NFSU2_DIR=%USERPROFILE%\OneDrive\Documents\NFSU2"

echo.
echo [BUILD] Configuring CMake (x86 Release)...
echo.

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -A Win32 -DCMAKE_BUILD_TYPE=Release "-DNFSU2_DIR=%NFSU2_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed. See output above.
    pause
    exit /b 1
)

:: ---------- Compile ----------
echo.
echo [BUILD] Compiling...
echo.

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed. See errors above.
    pause
    exit /b 1
)

:: ---------- Deploy config.ini ----------
if not exist "%NFSU2_DIR%\config.ini" (
    copy /Y "%SOURCE_DIR%config.ini" "%NFSU2_DIR%\config.ini" >nul
    echo [OK] config.ini copied to game folder
) else (
    echo [OK] config.ini already present - not overwritten
)

:: ---------- Done ----------
echo.
echo =====================================================
echo  BUILD COMPLETE
echo  Deployed: %NFSU2_DIR%\dinput8.dll
echo =====================================================
echo.
echo The new dinput8.dll includes a built-in ASI loader.
echo All mods in scripts\ will continue to work.
echo.
echo After launching NFSU2, check:
echo   %NFSU2_DIR%\logs\g29_ffb.log
echo.
pause
