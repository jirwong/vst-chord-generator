@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "SOURCE=%SCRIPT_DIR%build\ChordText_artefacts\Release\VST3\Chord Text.vst3"
set "DEST=C:\Program Files\Common Files\VST3"

if not exist "%SOURCE%" (
    echo [ERROR] VST3 build not found:
    echo         "%SOURCE%"
    echo.
    echo Build the Release configuration first:
    echo         build.bat
    exit /b 1
)

powershell -NoProfile -Command "if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { exit 1 }"
if errorlevel 1 (
    echo [ERROR] Administrator privileges are required to write to:
    echo         "%DEST%"
    echo.
    echo Right-click install-vst3.bat and choose "Run as administrator".
    exit /b 1
)

if not exist "%DEST%" (
    mkdir "%DEST%" >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Cannot create "%DEST%".
        exit /b 1
    )
)

echo Copying:
echo   from "%SOURCE%"
echo   to   "%DEST%\Chord Text.vst3"
echo.

robocopy "%SOURCE%" "%DEST%\Chord Text.vst3" /E /COPY:DAT /R:2 /W:1 /NFL /NDL /NP
if errorlevel 8 (
    echo.
    echo [ERROR] Copy failed. Close any DAW using the plugin and try again.
    exit /b 1
)

if not exist "%DEST%\Chord Text.vst3\Contents\x86_64-win\Chord Text.vst3" (
    echo.
    echo [ERROR] The copy finished, but the expected plugin binary was not found.
    exit /b 1
)

echo.
echo Installed "Chord Text.vst3" to "%DEST%".
echo Rescan plugins in your DAW.
exit /b 0
