#!/usr/bin/env bash
set -euo pipefail

cmake_cmd="$(command -v cmake || true)"
if [[ -z "$cmake_cmd" ]]; then
    for candidate in \
        "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/c/Program Files/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"; do
        if [[ -x "$candidate" ]]; then
            cmake_cmd="$candidate"
            break
        fi
    done
fi

if [[ -z "$cmake_cmd" ]]; then
    echo "CMake was not found on PATH or in the Visual Studio 2022 Build Tools installation." >&2
    exit 1
fi

if [[ ! -f build/CMakeCache.txt ]]; then
    "$cmake_cmd" -S . -B build -G "Visual Studio 17 2022" -A x64
fi

"$cmake_cmd" --build build --config Release --parallel

if [[ "${1:-}" == "--test" ]]; then
    ./build/Tests/TheoryTests.exe
    ./build/Tests/ProcessorTests.exe
    ./build/Tests/CliTests.exe
fi
