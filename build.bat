@echo off
setlocal

where cmake >nul 2>nul
if %errorlevel% equ 0 (
    set "CMAKE=cmake"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) else (
    echo CMake was not found on PATH or in the Visual Studio 2022 Build Tools installation. 1>&2
    exit /b 1
)

if not exist build\CMakeCache.txt (
    "%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 exit /b 1
)

"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 exit /b 1

if /I "%~1"=="test" (
    .\build\Tests\TheoryTests.exe
    if errorlevel 1 exit /b 1
    .\build\Tests\ProcessorTests.exe
    if errorlevel 1 exit /b 1
    exit /b 0
)

exit /b 0
