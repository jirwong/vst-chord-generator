# Repository guidance

## Build and verification

- Windows x64 only. C++17, Visual Studio 2022 Build Tools (Desktop C++), and CMake 3.22+. Use the `Visual Studio 17 2022` generator with `-A x64`, not Ninja.
- JUCE is not vendored. Before configuring, clone the documented version into ignored `libs/JUCE`:
  `git clone --depth 1 --branch 8.0.15 --recurse-submodules --shallow-submodules https://github.com/juce-framework/JUCE.git libs/JUCE`
- From the repo root, run `./build.sh --test` in Git Bash, or `build.bat test` in Command Prompt. Both locate CMake in VS Build Tools if absent from PATH, configure only when `build/CMakeCache.txt` is missing, build Release, then run all three suites, stopping on failure. Delete `build/` to force reconfiguration or change generators.
- Focused build: `cmake --build build --config Release --target ChordText --parallel`. Substitute `TheoryTests`, `ProcessorTests`, `CliTests`, or `ChordTextCli`. If CMake is absent from PATH, use the full path documented in README.
- Run suites directly: `./build/Tests/TheoryTests.exe`, `./build/Tests/ProcessorTests.exe`, `./build/Tests/CliTests.exe`. They use `Tests/Check.h`, have no per-case filter, and are not registered with CTest. `ProcessorTests` initializes JUCE GUI facilities; `CliTests` links the CLI core rather than launching `chordtext-cli.exe`.
- CLI smoke check without a DAW: `./build/Cli/chordtext-cli.exe --key A --mode minor "i-iv-V"`. The CMake target is `ChordTextCli`, not the output filename.
- VST3 output is `build/ChordText_artefacts/Release/VST3/Chord Text.vst3`; building does not install it. Copy the whole bundle, not just its inner binary.
- No CI workflow exists; verification is local. For code changes, report the VST3 build and all three suite results, and keep project code warning-free under JUCE's recommended flags. For docs-only changes, check content/links and `git diff --check`; see `.github/pull_request_template.md`. Automated tests do not establish DAW routing or drag acceptance; use README's manual checklist and explicitly report unperformed host checks.

## Boundaries and invariants

- `Source/Theory` and `Source/Cli` are JUCE-free. `Mode` covers nine scales; numeric degrees and Roman numerals resolve through the selected scale. Both plugin and CLI use the theory parser → voicer → render-plan pipeline and shared `MidiFileWriter`; do not duplicate music or MIDI-file logic in the UI/CLI.
- Add theory/CLI files to the shared `THEORY_SOURCES` / `CLI_CORE_SOURCES` lists in root `CMakeLists.txt`; add plugin files to `PLUGIN_SOURCES`. The plugin, test, and CLI targets are composed from those lists.
- In `Source/Plugin/PluginProcessor.*`, plan rebuild/publication and file writes belong to the message thread. Preserve the triple-buffer ownership between message and audio threads; never call `rebuildPlan`/`publishPlan` from `processBlock`. Export reads the cached host BPM; `AudioPlayHead::getPosition()` is processBlock-only.
- Live generation requires a host playhead with PPQ position and tempo; it does not free-run. `prepareToPlay` queues sounding-note releases for sample 0 of the next block; `releaseResources` can only clear tracking and cannot emit MIDI.
- Parse errors retain the plugin's last valid playback plan and learning rows, but disable export/drag. An empty progression is valid and replaces the plan with an empty one. CLI reports only current input, not stale valid rows.
- Export and CLI MIDI files include tempo and 4/4 metadata; drag clips deliberately omit both. Both paths share the render plan and writer, with note-offs before note-ons at equal times.
- Host state uses stable APVTS IDs `key`, `mode`, `voicing`, `range`, `velocity`, `loop`, `scaleMode`, plus `progressionText`. The editor selects one of nine scales through `scaleMode`; keep the two-value `mode` parameter for legacy automation and preserve the old-project state migration. Treat ID/value changes as saved-project compatibility changes.
- `SYNTAX.md` is the canonical user-facing grammar reference. Treat `docs/superpowers/` plans and specs as design records; verify implemented behavior in code.
