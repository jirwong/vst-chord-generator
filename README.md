# Chord Text

## Prerequisites

- Windows x64 with Visual Studio 2022 Build Tools and the C++ workload
- CMake 3.22 or newer
- Git

Clone JUCE 8.0.15 into the ignored `libs/JUCE` directory:

```bash
git clone --depth 1 --branch 8.0.15 --recurse-submodules --shallow-submodules https://github.com/juce-framework/JUCE.git libs/JUCE
```

Configure and build with the Visual Studio 2022 x64 generator:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
./build/Tests/ProcessorTests.exe
```

`build.sh --test` (Bash) and `build.bat test` (Command Prompt) configure and build
the Release targets, then run the processor tests.
