@echo off

setlocal EnableDelayedExpansion

cd /d "%~dp0"

set "SRC=%CD%"

rem 1. Locate VsDevCmd.bat

set "VSDEVCMD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\Common7\Tools\VsDevCmd.bat" (
            set "VSDEVCMD=%%i\Common7\Tools\VsDevCmd.bat"
        )
    )
)

rem Fallback search
if not defined VSDEVCMD (
    for /d %%p in ("%ProgramFiles%\Microsoft Visual Studio\*", "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do (
        if exist "%%p\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
        if exist "%%p\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2022\Community\Common7\Tools\VsDevCmd.bat"
        if exist "%%p\2019\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2019\Enterprise\Common7\Tools\VsDevCmd.bat"
        if exist "%%p\2019\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%p\2019\Community\Common7\Tools\VsDevCmd.bat"
    )
)

if not defined VSDEVCMD (
    echo [Error] Cannot find VsDevCmd.bat. Please ensure Visual Studio is installed.
    pause
    exit /b 1
)
echo [Info] Found Visual Studio env launcher at: %VSDEVCMD%

rem 2. Build x86 Version
call :BUILD_ARCH x86 5.01

rem 3. Build x64 Version
call :BUILD_ARCH x64 5.02

rem 4. Package Release with 7-Zip
echo.
echo ========================================
echo Packaging Release 7z Archive...
echo ========================================
if not exist "%SRC%\dist\release" mkdir "%SRC%\dist\release"

set "Z7_EXE=C:\Program Files\7-Zip\7z.exe"
if exist "!Z7_EXE!" (
    if exist "%SRC%\dist\release\UI_TouchKeyboard_X86_X64.7z" del /f /q "%SRC%\dist\release\UI_TouchKeyboard_X86_X64.7z"
    "!Z7_EXE!" a -t7z -mx=9 "%SRC%\dist\release\UI_TouchKeyboard_X86_X64.7z" "%SRC%\dist\x86\UI_TouchKeyboard_x86.exe" "%SRC%\dist\x64\UI_TouchKeyboard_x64.exe"
    if exist "%SRC%\dist\release\UI_TouchKeyboard_X86_X64.7z" (
        echo [Info] Successfully created release package: dist\release\UI_TouchKeyboard_X86_X64.7z
    ) else (
        echo [Error] Failed to create release package.
    )
) else (
    echo [Error] C:\Program Files\7-Zip\7z.exe not found. Skipping packaging.
)

echo.
echo ========================================
echo All Done! Outputs are in dist\x86, dist\x64 and dist\release
echo ========================================
pause
exit /b

rem ==============================================
rem Build Subroutine
rem ==============================================
:BUILD_ARCH
setlocal EnableDelayedExpansion
set "ARCH=%1"
set "SUBSYS_VER=%2"

echo.
echo ========================================
echo Building %ARCH% Version... [Subsystem: %SUBSYS_VER%]
echo ========================================

rem Init MSVC Env
call "%VSDEVCMD%" -arch=%ARCH% >nul 2>&1

rem Init directories
if not exist "%SRC%\build\%ARCH%" mkdir "%SRC%\build\%ARCH%"
if not exist "%SRC%\dist\%ARCH%" mkdir "%SRC%\dist\%ARCH%"

rem Define toolchain paths
set TOOLCHAIN_ROOT=M:\C++

set YY_THUNK="%TOOLCHAIN_ROOT%\YY-Thunks\build\native\objs\%ARCH%\YY_Thunks_for_WinXP.obj"

rem Inject VC-LTL paths
if "%ARCH%"=="x86" (
    set "LTL_VER=5.1.2600.0"
    if exist "%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\5.1.2600.0\lib\Win32" (
        set "LTL_LIB_SUB=lib\Win32"
    ) else if exist "%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\5.1.2600.0\lib\x86" (
        set "LTL_LIB_SUB=lib\x86"
    ) else (
        set "LTL_LIB_SUB=lib\!ARCH!"
    )
) else (
    set "LTL_VER=5.2.3790.0"
    if exist "%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\5.2.3790.0\lib\x64" (
        set "LTL_LIB_SUB=lib\x64"
    ) else (
        set "LTL_LIB_SUB=lib\!ARCH!"
    )
)

set "VCLTL_INC_DIR1=%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\header"
set "VCLTL_INC_DIR2=%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\!LTL_VER!\header"
set "VCLTL_LIB_DIR=%TOOLCHAIN_ROOT%\VC-LTL\build\native\TargetPlatform\!LTL_VER!\!LTL_LIB_SUB!"

set INCLUDE=!VCLTL_INC_DIR1!;!VCLTL_INC_DIR2!;!INCLUDE!
set LIB=!VCLTL_LIB_DIR!;!LIB!

rem Compile resource file (version info only, no icon)
echo [%ARCH%] Compiling resources...
rc /nologo /c65001 /fo "%SRC%\build\%ARCH%\touch_keyboard.res" "%SRC%\touch_keyboard.rc"
if !errorlevel! neq 0 exit /b !errorlevel!

rem Compile touch_keyboard.cpp
echo [%ARCH%] Compiling touch_keyboard.cpp ...
cl /nologo /utf-8 /O2 /MT /D_HAS_EXCEPTIONS=0 /GR- /d2FH4- /D_WIN32_WINNT=0x0501 /c "%SRC%\touch_keyboard.cpp" /Fo"%SRC%\build\%ARCH%\touch_keyboard.obj"
if !errorlevel! neq 0 exit /b !errorlevel!

rem Link
cl /nologo /utf-8 /O2 /MT /D_HAS_EXCEPTIONS=0 /GR- /d2FH4- "%SRC%\build\%ARCH%\touch_keyboard.obj" "%SRC%\build\%ARCH%\touch_keyboard.res" %YY_THUNK% /link /OUT:"%SRC%\dist\%ARCH%\UI_TouchKeyboard_%ARCH%.exe" /SUBSYSTEM:WINDOWS,%SUBSYS_VER% /MANIFEST:NO /CETCOMPAT:NO Comctl32.lib Shell32.lib Gdi32.lib User32.lib Advapi32.lib
if !errorlevel! neq 0 exit /b !errorlevel!

rem Compress with UPX
if exist "%SRC%\dist\%ARCH%\UI_TouchKeyboard_%ARCH%.exe" (
    if exist "%SRC%\bin\upx.exe" (
        echo [%ARCH%] Compressing final executable with UPX...
        "%SRC%\bin\upx.exe" --best --force "%SRC%\dist\%ARCH%\UI_TouchKeyboard_%ARCH%.exe"
    )
)

endlocal
exit /b
