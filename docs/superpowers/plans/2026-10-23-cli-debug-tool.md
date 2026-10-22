# Chord Text Command-Line Debug Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a JUCE-free `chordtext-cli.exe` that parses progression text, prints the same learning data and voicing as the plugin, supports a REPL and JSON, and writes MIDI through a writer shared with the plugin.

**Architecture:** Extract two pieces of plugin logic into `Source/Theory` (voiced-note naming and MIDI-file writing), then build a new `Source/Cli` core (settings, option parsing, report formatting, REPL, one-shot runner) that links only the theory sources. A thin Windows-aware `main.cpp` handles argv, stdin/TTY, UTF-8 consoles, and dispatch. `CliTests` exercises the core; existing `ProcessorTests` guard plugin parity.

**Tech Stack:** C++17, CMake 3.22+, Visual Studio 2022 x64, JUCE 8.0.15 for the plugin only, no third-party CLI libraries.

**Spec:** `docs/superpowers/specs/2026-10-23-cli-debug-tool-design.md`

## Global Constraints

- Windows x64 only; build with the Visual Studio 17 2022 x64 generator.
- C++17, no exceptions to the language standard.
- CLI executable and shared MIDI writer must not link JUCE.
- No new third-party dependencies.
- MSVC compiles with `/utf-8`; all source, tests, and README text is UTF-8.
- Build must produce no warnings from project code.
- Defaults: key C, major, voice-led, range C3-C6, velocity 100, BPM 120.
- MIDI files: SMF type 0, one track, 960 PPQ, track name `Chord Text`, channel 1, note-offs before note-ons at equal ticks, 4/4 plus tempo for normal exports, no tempo/time signature for drag clips.
- Keep the plugin's existing export/drag error messages and delete-on-failure behavior.
- Do not change chord syntax, theory behavior, or plugin UI.
- Exit codes: 0 parsed OK, 1 parse errors, 2 usage or file I/O failure.
- All three suites (`TheoryTests`, `ProcessorTests`, `CliTests`) must pass.
- Commit after each task; do not fold tasks together.

## Review Focus

1. **UTF-8 chord tokens on Windows** — `C°`, `Cø7`, and `Db` passed as argv or typed in the REPL must survive `wmain` → UTF-8 conversion and render correctly in tables and JSON. A parse error or mojibake here is a bug. Tested in Tasks 5 and 8.
2. **Slash bass and inversions in `Notes`** — `C/E` root-position must show `E C E G` with voicing `[52,60,64,67]` (slash bass duplicated), and inversions must list notes in voicing order. Using chord-order spelling would break plugin parity. Tested in Tasks 1 and 5.
3. **Output path handling** — no extension, a non-`.mid` extension, uppercase `.MID`, relative paths, paths with spaces, and unwritable parents must resolve as specified, never overwrite the wrong file, and leave no partial file on failure. Tested in Tasks 4, 6, and 7.
4. **Fractional durations and locale** — `:1.5`, `:0.25`, and long repeating decimals must round-trip in the table and JSON, and a comma-decimal system locale must not produce invalid JSON. Tested in Tasks 4 and 5.
5. **Piped and multi-line input** — CRLF line endings, blank/whitespace-only input, and closed stdin must not hang or fail; the human header collapses to one line while an empty progression stays valid. Tested in Tasks 5 and 8.

---

### Task 1: Extract shared voiced-note naming

**Files:**
- Modify: `Source/Theory/Chord.h`
- Modify: `Source/Theory/Chord.cpp`
- Modify: `Source/Plugin/PluginProcessor.cpp` (remove local helpers at the top; update the call in `rebuildPlan`)
- Test: `Tests/TheoryTest.cpp`

**Interfaces:**
- Consumes: `chordtext::Chord::tones()`, `chordtext::PitchClass`.
- Produces:
  - `std::string chordtext::noteNameForPitch (const Chord& chord, int midi)`
  - `std::string chordtext::notesForVoicing (const Chord& chord, const std::vector<int>& voicing)`

- [ ] **Step 1: Write the failing tests**

In `Tests/TheoryTest.cpp`, after the existing `names` lambda and chord checks, add:

```cpp
CHECK (noteNameForPitch (chord ({'C', 1}, ""), 65) == "E#");
CHECK (noteNameForPitch (chord ({'C', 1}, ""), 62) == "D");
CHECK (notesForVoicing (chord ({'C', 0}, "maj7"), {48, 52, 55, 59}) == "C E G B");
CHECK (notesForVoicing (chord ({'G', 0}, "7"), {62, 65, 67, 71}) == "D F G B");
CHECK (notesForVoicing (chord ({'C', 0}, "", {}, PitchClass {'E', 0}),
                        {52, 60, 64, 67}) == "E C E G");
```

Expected: `C#` major's E# is spelled `E#` for MIDI 65 (pitch class F); MIDI 62 has no chord tone and falls back to `D`; the G7 inversion keeps voicing order; the C/E slash bass repeats E.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
./build.sh
./build/Tests/TheoryTests.exe
```

Expected: compile error or failed checks for the missing `noteNameForPitch` / `notesForVoicing`.

- [ ] **Step 3: Implement the shared functions**

Declare both functions in `Source/Theory/Chord.h` after `degreeLabel`:

```cpp
std::string noteNameForPitch (const Chord& chord, int midi);
std::string notesForVoicing (const Chord& chord, const std::vector<int>& voicing);
```

Implement in `Source/Theory/Chord.cpp`:

- `noteNameForPitch`: normalise `midi` to a 0-11 pitch class, scan `chord.tones()` for the first tone with the same normalised semitone, and return `tone.name()`. If none matches, return the sharp-based pitch-class name from a local `std::array<const char*, 12> {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}`. No octave number.
- `notesForVoicing`: loop over `voicing` in order, call `noteNameForPitch`, and join names with a single space.

In `Source/Plugin/PluginProcessor.cpp`:

- Delete the anonymous-namespace `noteNameForPitch` and `notesForVoicing`.
- Replace the `row.notes = notesForVoicing (step.chord, voicings[index]);` call with `chordtext::notesForVoicing`.
- Add `#include "Theory/Chord.h"` if it is not already visible.

- [ ] **Step 4: Run both suites**

```bash
./build.sh --test
```

Expected: `TheoryTests` and `ProcessorTests` both report 0 failures. The plugin's learning rows are unchanged.

- [ ] **Step 5: Commit**

```bash
git add Source/Theory/Chord.h Source/Theory/Chord.cpp Source/Plugin/PluginProcessor.cpp Tests/TheoryTest.cpp
git commit -m "refactor: share voiced-note naming between plugin and CLI"
```

---

### Task 2: Add the JUCE-free MIDI file writer

**Files:**
- Create: `Source/Theory/MidiFileWriter.h`
- Create: `Source/Theory/MidiFileWriter.cpp`
- Modify: `CMakeLists.txt` (add `Source/Theory/MidiFileWriter.cpp` to `THEORY_SOURCES`)
- Test: `Tests/TheoryTest.cpp`

**Interfaces:**
- Consumes: `chordtext::RenderPlan` and `chordtext::MidiEvent`.
- Produces:

```cpp
namespace chordtext
{
struct MidiFileSettings
{
    double bpm = 120.0;
    bool includeTempoAndTimeSignature = true;
    std::string trackName = "Chord Text";
    int ticksPerQuarterNote = 960;
};

std::vector<std::uint8_t> serialiseMidiFile (const RenderPlan& plan,
                                             const MidiFileSettings& settings);

bool writeMidiFile (const RenderPlan& plan,
                    const std::filesystem::path& file,
                    const MidiFileSettings& settings,
                    std::string& error);
}
```

- [ ] **Step 1: Write the failing tests**

Add a test-only SMF reader near the top of `Tests/TheoryTest.cpp` (inside the existing file, before `main`):

```cpp
struct TestMidiEvent
{
    int tick = 0;
    bool noteOn = false;
    int pitch = 0;
    int velocity = 0;
    int channel = 1;
};

static int readU32 (const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (int (bytes[offset]) << 24) | (int (bytes[offset + 1]) << 16)
         | (int (bytes[offset + 2]) << 8) | int (bytes[offset + 3]);
}

static int readVlq (const std::vector<std::uint8_t>& bytes, std::size_t& offset)
{
    int value = 0;
    for (;;)
    {
        const auto byte = bytes[offset++];
        value = (value << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0)
            return value;
    }
}

static std::vector<TestMidiEvent> readNoteEvents (const std::vector<std::uint8_t>& bytes)
{
    std::vector<TestMidiEvent> result;
    auto offset = std::size_t { 8 + readU32 (bytes, 4) };
    const auto trackEnd = offset + 8 + readU32 (bytes, offset + 4);
    offset += 8;
    int tick = 0;
    int running = 0;

    while (offset < trackEnd)
    {
        tick += readVlq (bytes, offset);
        auto status = bytes[offset++];
        if (status < 0x80)
        {
            --offset;
            status = running;
        }
        else
        {
            running = status;
        }

        if (status == 0xFF)
        {
            const auto type = bytes[offset++];
            const auto length = readVlq (bytes, offset);
            offset += std::size_t (length);
            if (type == 0x2F)
                break;
            continue;
        }

        const auto type = status & 0xF0;
        const auto channel = (status & 0x0F) + 1;
        const auto data1 = int (bytes[offset++]);
        const auto data2 = (type == 0xC0 || type == 0xD0) ? 0 : int (bytes[offset++]);

        if (type == 0x90 && data2 > 0)
            result.push_back ({tick, true, data1, data2, channel});
        else if (type == 0x80 || (type == 0x90 && data2 == 0))
            result.push_back ({tick, false, data1, data2, channel});
    }

    return result;
}

static bool containsBytes (const std::vector<std::uint8_t>& bytes,
                           std::initializer_list<std::uint8_t> pattern)
{
    return std::search (bytes.begin(), bytes.end(), pattern.begin(), pattern.end())
        != bytes.end();
}
```

Add includes `#include <algorithm>`, `#include <cstdint>`, `#include <filesystem>`, `#include <fstream>`, `#include <vector>` to the test file.

Inside `main`, add:

```cpp
RenderPlan midiPlan;
midiPlan.lengthBeats = 4.0;
midiPlan.events = {
    {4.0, 64, 0, 1, false, 0},
    {4.0, 60, 0, 1, false, 0},
    {0.0, 64, 100, 1, true, 0},
    {0.0, 60, 100, 1, true, 0},
};

const MidiFileSettings exportSettings;
const auto bytes = serialiseMidiFile (midiPlan, exportSettings);
CHECK (std::string (bytes.begin(), bytes.begin() + 4) == "MThd");
CHECK (readU32 (bytes, 4) == 6);
CHECK ((bytes[8] == 0 && bytes[9] == 0));                 // format 0
CHECK ((bytes[10] == 0 && bytes[11] == 1));               // one track
CHECK ((bytes[12] == 0x03 && bytes[13] == 0xC0));         // 960 PPQ
CHECK (containsBytes (bytes, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20})); // 500000 us
CHECK (containsBytes (bytes, {0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08}));
CHECK (containsBytes (bytes, {0xFF, 0x2F, 0x00}));
CHECK (bytes.back() == 0x00);
CHECK (serialiseMidiFile (midiPlan, exportSettings) == bytes);

const auto events = readNoteEvents (bytes);
CHECK (events.size() == 4);
CHECK (events[0].tick == 0 && events[0].noteOn && events[0].pitch == 60
       && events[0].velocity == 100 && events[0].channel == 1);
CHECK (events[1].tick == 0 && events[1].noteOn && events[1].pitch == 64);
CHECK (events[2].tick == 3840 && ! events[2].noteOn && events[2].pitch == 60);
CHECK (events[3].tick == 3840 && ! events[3].noteOn && events[3].pitch == 64);

auto dragSettings = exportSettings;
dragSettings.includeTempoAndTimeSignature = false;
const auto dragBytes = serialiseMidiFile (midiPlan, dragSettings);
CHECK (! containsBytes (dragBytes, {0xFF, 0x51}));
CHECK (! containsBytes (dragBytes, {0xFF, 0x58}));
CHECK (containsBytes (dragBytes, {0xFF, 0x2F, 0x00}));

const auto tempFile = std::filesystem::temp_directory_path()
                          / "chordtext-midi-writer-test.mid";
std::filesystem::remove (tempFile);
std::string writeError;
CHECK (writeMidiFile (midiPlan, tempFile, exportSettings, writeError));
CHECK (writeError.empty());
CHECK (std::filesystem::exists (tempFile));
std::filesystem::remove (tempFile);

const auto notADirectory = std::filesystem::temp_directory_path()
                              / "chordtext-midi-writer-blocker";
std::filesystem::remove_all (notADirectory);
{
    std::ofstream blockerFile (notADirectory);
    blockerFile << "x";
}
CHECK (! writeMidiFile (midiPlan, notADirectory / "out.mid", exportSettings, writeError));
CHECK (! writeError.empty());
std::filesystem::remove (notADirectory);
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
./build.sh
./build/Tests/TheoryTests.exe
```

Expected: compile/link failure for the missing writer.

- [ ] **Step 3: Implement the writer**

Create `Source/Theory/MidiFileWriter.h` with the exact interface above; include `"Theory/RenderPlan.h"`, `<cstdint>`, `<filesystem>`, `<string>`, `<vector>`.

Create `Source/Theory/MidiFileWriter.cpp`:

- Header chunk: `MThd`, length `00 00 00 06`, format `00 00`, tracks `00 01`, division from `settings.ticksPerQuarterNote` as a big-endian 16-bit value.
- Build the track event byte stream in this order at tick 0:
  1. track-name meta: `FF 03`, VLQ byte length of `trackName`, UTF-8 bytes;
  2. if `includeTempoAndTimeSignature`: tempo meta `FF 51 03` plus a big-endian 24-bit `lround(60000000 / bpm)` (fall back to 120 BPM when `bpm` is not finite or <= 0, clamp the value to 1..0xFFFFFF), then time signature `FF 58 04 04 02 18 08`;
  3. sorted notes.
- Before emitting notes, copy the event list, then `std::stable_sort` by beat ascending, note-offs before note-ons at the same beat, then pitch ascending.
- For each note, compute `tick = llround(beat * ticksPerQuarterNote)`, emit a VLQ delta from the previous tick, then `0x90 | (channel - 1)` or `0x80 | (channel - 1)`, pitch, and velocity (note-offs use 0). Use `event.channel` as the 1-based channel.
- Finish with delta 0 and `FF 2F 00`.
- Write the track chunk as `MTrk`, a big-endian 32-bit length, then the event bytes.
- Implement standard MIDI VLQ encoding (7 bits per byte, high bit set on every byte except the last).
- `writeMidiFile`: clear `error`, open `std::ofstream (file, std::ios::binary | std::ios::trunc)`. On failure set `error = "Cannot open " + file.u8string() + " for writing."` and return false. Write all bytes; on failure or short write set `error = "Cannot write the MIDI file " + file.u8string() + "."` and return false. Do not delete partial files.

Add `Source/Theory/MidiFileWriter.cpp` to `THEORY_SOURCES` in `CMakeLists.txt`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
./build.sh
./build/Tests/TheoryTests.exe
```

Expected: 0 failures. The header, tempo, time signature, ordering, determinism, written file, and I/O failure are covered.

- [ ] **Step 5: Commit**

```bash
git add Source/Theory/MidiFileWriter.h Source/Theory/MidiFileWriter.cpp CMakeLists.txt Tests/TheoryTest.cpp
git commit -m "feat: add shared JUCE-free MIDI file writer"
```

---

### Task 3: Route the plugin through the shared writer

**Files:**
- Modify: `Source/Plugin/PluginProcessor.cpp` (`writeMidiFile`, includes)

**Interfaces:**
- Consumes: `chordtext::MidiFileSettings`, `chordtext::writeMidiFile`.
- Produces: unchanged `ChordTextAudioProcessor::writeMidiFile` signature and behavior.

- [ ] **Step 1: Record the baseline**

```bash
./build.sh --test
```

Expected: `TheoryTests` and `ProcessorTests` pass with 0 failures.

- [ ] **Step 2: Refactor `writeMidiFile`**

In `Source/Plugin/PluginProcessor.cpp`:

- Add `#include "Theory/MidiFileWriter.h"` and `#include <filesystem>`.
- Keep the existing diagnostics and empty-plan guards and their exact messages.
- Replace the `juce::MidiFile` construction, sorting, and writing after the guards with:

```cpp
chordtext::MidiFileSettings settings;
const auto cachedBpm = lastHostBpm.load (std::memory_order_relaxed);
settings.bpm = cachedBpm > 0.0 && std::isfinite (cachedBpm) ? cachedBpm : 120.0;
settings.includeTempoAndTimeSignature = mode == MidiFileMode::exportFile;

std::string error;
if (! chordtext::writeMidiFile (
        exportPlan,
        std::filesystem::u8path (file.getFullPathName().toStdString()),
        settings,
        error))
{
    errorMessage = juce::String::fromUTF8 (error.c_str());
    return false;
}

return true;
```

- Remove the now-unused `ticksPerQuarterNote` constant and `juce::MidiFile` locals from that function only. Do not touch other processor behavior.
- `createDragClipFile` still deletes the file when `writeMidiFile` returns false.

- [ ] **Step 3: Run all suites**

```bash
./build.sh --test
```

Expected: `TheoryTests` and `ProcessorTests` pass. The existing export and drag read-back checks prove the plugin still writes tempo/time-signature for exports and omits them for drag clips, with the same notes and ordering.

- [ ] **Step 4: Commit**

```bash
git add Source/Plugin/PluginProcessor.cpp
git commit -m "refactor: use shared MIDI writer in the plugin"
```

---

### Task 4: CLI settings, option parsing, paths, and the CliTests target

**Files:**
- Create: `Source/Cli/CliSettings.h`
- Create: `Source/Cli/CliSettings.cpp`
- Create: `Source/Cli/CliOptions.h`
- Create: `Source/Cli/CliOptions.cpp`
- Create: `Source/Cli/CliFile.h`
- Create: `Source/Cli/CliFile.cpp`
- Create: `Tests/CliTest.cpp`
- Modify: `CMakeLists.txt`
- Modify: `build.sh`
- Modify: `build.bat`

**Interfaces:**
- Consumes: `chordtext::Key`, `chordtext::Mode`, `chordtext::VoicingMode`, `chordtext::VoicingSettings`, `chordtext::PitchClass`, `chordtext::MidiFileSettings`.
- Produces:

```cpp
namespace chordtext::cli
{
struct CliSettings
{
    PitchClass tonic { 'C', 0 };
    Mode mode = Mode::major;
    VoicingMode voicing = VoicingMode::voiceLed;
    int rangeIndex = 1;
    int velocity = 100;
    double bpm = 120.0;
    bool json = false;

    Key key() const;
    VoicingSettings voicingSettings() const;
    MidiFileSettings midiFileSettings() const;
    std::string keyName() const;
    std::string modeName() const;
    std::string voicingName() const;
    std::string rangeName() const;
};

std::string formatSettingsLine (const CliSettings& settings);

std::optional<PitchClass> parseTonic (std::string_view text);
std::optional<Mode> parseMode (std::string_view text);
std::optional<VoicingMode> parseVoicing (std::string_view text);
std::optional<int> parseRangeIndex (std::string_view text);
std::optional<int> parseVelocity (std::string_view text);
std::optional<double> parseBpm (std::string_view text);

struct CliOptions
{
    CliSettings settings;
    std::optional<std::string> progression;
    std::optional<std::string> outputPath;
};

enum class CliParseStatus { ok, help, usageError };

struct CliParseResult
{
    CliParseStatus status = CliParseStatus::ok;
    CliOptions options;
    std::string error;
};

CliParseResult parseOptions (const std::vector<std::string>& args);
std::string helpText();

std::filesystem::path resolveMidiPath (std::string_view userPath);
}
```

- [ ] **Step 1: Write the failing tests**

Create `Tests/CliTest.cpp`:

```cpp
#include "Check.h"
#include "Cli/CliFile.h"
#include "Cli/CliOptions.h"
#include "Cli/CliSettings.h"

using namespace chordtext;
using namespace chordtext::cli;

int main()
{
    const CliSettings defaults;
    CHECK (defaults.key().tonic().name() == "C");
    CHECK (defaults.mode == Mode::major);
    CHECK (defaults.voicing == VoicingMode::voiceLed);
    CHECK (defaults.rangeIndex == 1);
    CHECK (defaults.velocity == 100);
    CHECK (defaults.bpm == 120.0);
    CHECK (! defaults.json);
    CHECK (defaults.keyName() == "C");
    CHECK (defaults.modeName() == "major");
    CHECK (defaults.voicingName() == "voice-led");
    CHECK (defaults.rangeName() == "C3-C6");
    CHECK (formatSettingsLine (defaults)
           == "Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100");

    const auto midiSettings = defaults.midiFileSettings();
    CHECK (midiSettings.bpm == 120.0);
    CHECK (midiSettings.includeTempoAndTimeSignature);
    CHECK (midiSettings.trackName == "Chord Text");
    CHECK (midiSettings.ticksPerQuarterNote == 960);

    CHECK (parseTonic ("db")->name() == "Db");
    CHECK (! parseTonic ("H").has_value());
    CHECK (parseMode ("MINOR") == std::optional<Mode> { Mode::minor });
    CHECK (! parseMode ("dorian").has_value());
    CHECK (parseVoicing ("ROOT-POSITION")
           == std::optional<VoicingMode> { VoicingMode::rootPosition });
    CHECK (parseVoicing ("root")
           == std::optional<VoicingMode> { VoicingMode::rootPosition });
    CHECK (! parseVoicing ("close").has_value());
    CHECK (parseRangeIndex ("c2-c5") == std::optional<int> { 0 });
    CHECK (parseRangeIndex ("C4-C7") == std::optional<int> { 2 });
    CHECK (! parseRangeIndex ("C1-C2").has_value());
    CHECK (parseVelocity ("127") == std::optional<int> { 127 });
    CHECK (! parseVelocity ("0").has_value());
    CHECK (! parseVelocity ("128").has_value());
    CHECK (! parseVelocity ("12x").has_value());
    CHECK (parseBpm ("96.5") == std::optional<double> { 96.5 });
    CHECK (! parseBpm ("3.9").has_value());
    CHECK (! parseBpm ("1000").has_value());
    CHECK (! parseBpm ("nan").has_value());

    auto parsed = parseOptions ({});
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (! parsed.options.progression.has_value());
    CHECK (! parsed.options.outputPath.has_value());
    CHECK (parsed.options.settings.keyName() == "C");

    parsed = parseOptions ({ "--key", "A", "--mode=minor", "i-iv-V" });
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (parsed.options.settings.keyName() == "A");
    CHECK (parsed.options.settings.mode == Mode::minor);
    CHECK (parsed.options.progression == std::optional<std::string> { "i-iv-V" });

    parsed = parseOptions ({ "--key=C#", "--range", "C4-C7", "--velocity=64",
                             "--bpm=90", "--json", "--out", "loop" });
    CHECK (parsed.options.settings.keyName() == "C#");
    CHECK (parsed.options.settings.rangeIndex == 2);
    CHECK (parsed.options.settings.velocity == 64);
    CHECK (parsed.options.settings.bpm == 90.0);
    CHECK (parsed.options.settings.json);
    CHECK (parsed.options.outputPath == std::optional<std::string> { "loop" });

    parsed = parseOptions ({ "--key", "C", "--key", "D" });
    CHECK (parsed.options.settings.keyName() == "D");

    parsed = parseOptions ({ "--", "-C" });
    CHECK (parsed.options.progression == std::optional<std::string> { "-C" });

    parsed = parseOptions ({ "--help", "--key", "H" });
    CHECK (parsed.status == CliParseStatus::help);

    const std::vector<std::vector<std::string>> usageErrors {
        { "--key", "H" },
        { "--key" },
        { "--mystery" },
        { "C", "D" },
        { "--json=on" },
        { "--velocity", "0" },
        { "--bpm", "3.9" }
    };

    for (const auto& args : usageErrors)
    {
        const auto result = parseOptions (args);
        CHECK (result.status == CliParseStatus::usageError);
        CHECK (! result.error.empty());
    }

    CHECK (! helpText().empty());
    CHECK (helpText().find ("--key") != std::string::npos);
    CHECK (helpText().find ("--out") != std::string::npos);

    CHECK (resolveMidiPath ("loop").filename().u8string() == "loop.mid");
    CHECK (resolveMidiPath ("loop").is_absolute());
    CHECK (resolveMidiPath ("loop.txt").filename().u8string() == "loop.mid");
    CHECK (resolveMidiPath ("loop.MID").filename().u8string() == "loop.MID");

    return reportAndExit();
}
```

- [ ] **Step 2: Add the target and run the tests to verify they fail**

In `CMakeLists.txt`, after `THEORY_SOURCES` and before the plugin targets, add:

```cmake
set(CLI_CORE_SOURCES
    Source/Cli/CliSettings.cpp
    Source/Cli/CliOptions.cpp
    Source/Cli/CliFile.cpp)
```

After the `TheoryTests` target, add:

```cmake
add_executable(CliTests
    Tests/CliTest.cpp
    ${CLI_CORE_SOURCES}
    ${THEORY_SOURCES})
target_include_directories(CliTests PRIVATE Source)
target_compile_features(CliTests PRIVATE cxx_std_17)
set_target_properties(CliTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                 "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG           "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE         "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL      "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO  "${CMAKE_BINARY_DIR}/Tests")
```

Add the third suite to both scripts:

- `build.sh`, in the `--test` block, after `ProcessorTests`:
  ```bash
  ./build/Tests/CliTests.exe
  ```
- `build.bat`, in the `test` block, after `ProcessorTests`:
  ```bat
  .\build\Tests\CliTests.exe
  if errorlevel 1 exit /b 1
  ```

Run:

```bash
./build.sh
./build/Tests/CliTests.exe
```

Expected: compile/link failure for the missing CLI sources.

- [ ] **Step 3: Implement settings, options, and paths**

`CliSettings.cpp`:

- Range tables `{36,48,60}` / `{72,84,96}`; clamp `rangeIndex` to 0..2.
- `key()` returns `Key { tonic, mode }`; `voicingSettings()` returns `{ voicing, lows[index], highs[index] }`.
- `midiFileSettings()` returns `{ bpm, true, "Chord Text", 960 }`.
- `keyName()` = `tonic.name()`; `modeName()` = `"major"`/`"minor"`; `voicingName()` = `"voice-led"`/`"root-position"`; `rangeName()` = `"C2-C5"`/`"C3-C6"`/`"C4-C7"`.
- `formatSettingsLine` returns `Key: <key> <mode> · Voicing: <voicing> · Range: <range> · Velocity: <n>`.
- `parseTonic`: case-insensitive match against the 17 names `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B`; build `PitchClass { name[0], name[1] == '#' ? 1 : name[1] == 'b' ? -1 : 0 }`.
- `parseMode`, `parseVoicing`, `parseRangeIndex`: case-insensitive exact match; `root` maps to `rootPosition`.
- `parseVelocity`: `std::from_chars` int, full consumption, 1..127.
- `parseBpm`: `std::from_chars` double with `std::chars_format::general`, full consumption, finite, 4.0..999.0.
- All parsers return `std::nullopt` on failure.

`CliOptions.cpp`:

- Pre-scan for an exact `--help` token before any `--`; if present, return `{ help, {}, {} }`.
- Iterate arguments after the program name.
- `--` switches to positional-only mode for the rest.
- `--name=value` and `--name value` are supported for value options; `--json` rejects an attached value.
- Unknown option → `{ usageError, {}, "Unknown option: " + arg }`.
- Missing value → `{ usageError, {}, "Missing value for " + name }`.
- Invalid value → `{ usageError, {}, "Invalid value for " + name + ": " + value }`.
- More than one positional → usage error containing `Unexpected positional argument`.
- Repeated options: last wins.
- Map options to `CliSettings` fields; `--out` sets `outputPath`.
- `helpText()` returns a usage block with the command line, every option, defaults, and the exit codes.

`CliFile.cpp`:

- `resolveMidiPath`: `std::filesystem::u8path (std::string (userPath))`; if relative, `std::filesystem::absolute`; if `path.extension()` is not `.mid` (ASCII case-insensitive), `path.replace_extension (".mid")`. Return the result. No filesystem existence checks.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
./build.sh --test
```

Expected: `TheoryTests`, `ProcessorTests`, and `CliTests` all report 0 failures.

- [ ] **Step 5: Commit**

```bash
git add Source/Cli/CliSettings.h Source/Cli/CliSettings.cpp Source/Cli/CliOptions.h Source/Cli/CliOptions.cpp Source/Cli/CliFile.h Source/Cli/CliFile.cpp Tests/CliTest.cpp CMakeLists.txt build.sh build.bat
git commit -m "feat: add CLI settings, option parsing, and CliTests target"
```

---

### Task 5: Build and format reports

**Files:**
- Create: `Source/Cli/CliReport.h`
- Create: `Source/Cli/CliReport.cpp`
- Modify: `CMakeLists.txt` (add `Source/Cli/CliReport.cpp` to `CLI_CORE_SOURCES`)
- Test: `Tests/CliTest.cpp`

**Interfaces:**
- Consumes: `chordtext::parseProgression`, `voiceProgression`, `buildRenderPlan`, `degreeLabel`, `noteNameForPitch`, `CliSettings`.
- Produces:

```cpp
namespace chordtext::cli
{
struct CliChordRow
{
    std::string source;
    std::string symbol;
    std::string degree;
    std::vector<std::string> spelledTones;
    std::vector<std::string> notes;
    std::vector<int> voicing;
    double startBeat = 0.0;
    double durationBeats = 4.0;
};

struct CliReport
{
    std::string text;
    CliSettings settings;
    bool valid = false;
    std::vector<Diagnostic> diagnostics;
    std::vector<CliChordRow> chords;
    RenderPlan plan;
    std::optional<std::string> midiFile;
    std::optional<std::string> error;
};

CliReport buildReport (std::string_view text, const CliSettings& settings);
std::string formatHuman (const CliReport& report);
std::string formatJson (const CliReport& report);
}
```

- [ ] **Step 1: Write the failing tests**

Add to `Tests/CliTest.cpp` (before `return reportAndExit();`), and include `"Cli/CliReport.h"`:

```cpp
CliSettings tableSettings;
tableSettings.voicing = VoicingMode::rootPosition;
tableSettings.rangeIndex = 0;

const auto report = buildReport ("Cmaj7:2, Am7", tableSettings);
CHECK (report.valid);
CHECK (report.diagnostics.empty());
CHECK (report.chords.size() == 2);
CHECK (report.chords[0].source == "Cmaj7");
CHECK (report.chords[0].symbol == "Cmaj7");
CHECK (report.chords[0].degree == "Imaj7");
CHECK ((report.chords[0].spelledTones
        == std::vector<std::string> { "C", "E", "G", "B" }));
CHECK ((report.chords[0].voicing == std::vector<int> { 36, 40, 43, 47 }));
CHECK ((report.chords[0].notes
        == std::vector<std::string> { "C", "E", "G", "B" }));
CHECK (report.chords[0].startBeat == 0.0);
CHECK (report.chords[0].durationBeats == 2.0);
CHECK (report.chords[1].startBeat == 2.0);
CHECK (report.chords[1].durationBeats == 4.0);
CHECK (report.plan.lengthBeats == 6.0);
CHECK (report.plan.events.size() == 16);

const std::string expectedHuman =
    "Progression: Cmaj7:2, Am7\n"
    "Key: C major · Voicing: root-position · Range: C2-C5 · Velocity: 100\n"
    "\n"
    "Chord  Degree  Notes    Beats  MIDI\n"
    "Cmaj7  Imaj7   C E G B  2      36 40 43 47\n"
    "Am7    vi7     A C E G  4      45 48 52 55\n";
CHECK (formatHuman (report) == expectedHuman);

const auto invalid = buildReport ("C, H, C99", CliSettings {});
CHECK (! invalid.valid);
CHECK (invalid.diagnostics.size() == 2);
const std::string expectedInvalid =
    "Progression: C, H, C99\n"
    "Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100\n"
    "\n"
    "2 errors:\n"
    "Error at character 3: unknown chord root (H)\n"
    "Error at character 6: unknown extension \"99\" (C99)\n";
CHECK (formatHuman (invalid) == expectedInvalid);

const auto empty = buildReport ("   ", CliSettings {});
CHECK (empty.valid);
CHECK (empty.chords.empty());
CHECK (formatHuman (empty).find ("Progression: (empty)\n") == 0);
CHECK (formatHuman (empty).find ("Empty progression (no chords).\n")
       != std::string::npos);

const auto multiLine = buildReport ("C\n  Am7\tF", CliSettings {});
CHECK (formatHuman (multiLine).find ("Progression: C Am7 F\n") == 0);

const auto dim = buildReport ("C°", CliSettings {});
CHECK (dim.valid);
CHECK (dim.chords[0].symbol == "Cdim");
CHECK (formatJson (dim).find ("\"symbol\": \"Cdim\"") != std::string::npos);
const auto halfDim = buildReport ("Cø7", CliSettings {});
CHECK (halfDim.valid);
CHECK (halfDim.chords[0].symbol == "Cm7b5");

const auto slash = buildReport ("C/E", CliSettings {});
CHECK (slash.valid);
CHECK ((slash.chords[0].voicing == std::vector<int> { 52, 60, 64, 67 }));
CHECK ((slash.chords[0].notes
        == std::vector<std::string> { "E", "C", "E", "G" }));

const auto fractions = buildReport ("Cmaj7:1.5, Am7:0.25, G7:0.3333333333333333",
                                    CliSettings {});
CHECK (fractions.chords[0].durationBeats == 1.5);
CHECK (fractions.chords[1].durationBeats == 0.25);
const auto fractionsJson = formatJson (fractions);
CHECK (fractionsJson.find ("\"durationBeats\": 1.5") != std::string::npos);
CHECK (fractionsJson.find ("\"durationBeats\": 0.25") != std::string::npos);
CHECK (fractionsJson.find ("\"durationBeats\": 0.3333333333333333")
       != std::string::npos);

const auto humanJson = formatJson (report);
CHECK (humanJson.find ("\"valid\": true") != std::string::npos);
CHECK (humanJson.find ("\"key\": \"C\"") != std::string::npos);
CHECK (humanJson.find ("\"voicing\": \"root-position\"") != std::string::npos);
CHECK (humanJson.find ("\"range\": \"C2-C5\"") != std::string::npos);
CHECK (humanJson.find ("\"velocity\": 100") != std::string::npos);
CHECK (humanJson.find ("\"bpm\": 120") != std::string::npos);
CHECK (humanJson.find ("\"source\": \"Cmaj7\"") != std::string::npos);
CHECK (humanJson.find ("\"spelledTones\": [\"C\", \"E\", \"G\", \"B\"]")
       != std::string::npos);
CHECK (humanJson.find ("\"notes\": [\"C\", \"E\", \"G\", \"B\"]")
       != std::string::npos);
CHECK (humanJson.find ("\"voicing\": [36, 40, 43, 47]") != std::string::npos);
CHECK (humanJson.find ("\"startBeat\": 0") != std::string::npos);
CHECK (humanJson.find ("\"durationBeats\": 2") != std::string::npos);
CHECK (humanJson.find ("\"midiFile\": null") != std::string::npos);
CHECK (humanJson.find ("\"error\": null") != std::string::npos);
CHECK (humanJson.back() == '\n');

const auto invalidJson = formatJson (invalid);
CHECK (invalidJson.find ("\"valid\": false") != std::string::npos);
CHECK (invalidJson.find ("\"chords\": []") != std::string::npos);
CHECK (invalidJson.find ("\"offset\": 3") != std::string::npos);
CHECK (invalidJson.find ("\"token\": \"H\"") != std::string::npos);
CHECK (invalidJson.find ("\"message\": \"unknown chord root\"")
       != std::string::npos);

const auto escaped = buildReport (std::string ("a\"b\\c\nd"), CliSettings {});
CHECK (formatJson (escaped).find ("\"text\": \"a\\\"b\\\\c\\nd\"")
       != std::string::npos);

auto withFile = report;
withFile.midiFile = "C:\\tmp\\x.mid";
CHECK (formatJson (withFile).find ("\"midiFile\": \"C:\\\\tmp\\\\x.mid\"")
       != std::string::npos);
withFile.error = "bad \"path\"";
CHECK (formatJson (withFile).find ("\"error\": \"bad \\\"path\\\"\"")
       != std::string::npos);
```

- [ ] **Step 2: Run the tests to verify they fail**

Add `Source/Cli/CliReport.cpp` to `CLI_CORE_SOURCES`, then run:

```bash
./build.sh
./build/Tests/CliTests.exe
```

Expected: compile/link failure for `buildReport` / `formatHuman` / `formatJson`.

- [ ] **Step 3: Implement the report pipeline and formatters**

Create `CliReport.h` with the exact interface above; include `"Cli/CliSettings.h"`, `"Theory/Diagnostic.h"`, `"Theory/RenderPlan.h"`, `<optional>`, `<string>`, `<string_view>`, `<vector>`.

`buildReport`:

- Store `text` verbatim and `settings`.
- `parseProgression (report.text, settings.key())`; copy diagnostics.
- If `! parsed.progression.has_value()`, return with `valid = false`, empty chords/plan.
- Set `valid = true`; `voicings = voiceProgression (*parsed.progression, settings.voicingSettings())`; `plan = buildRenderPlan (*parsed.progression, voicings, settings.velocity)`.
- For each step/index: fill `source`, `symbol`, `degree`; append each `chord.tones()` name to `spelledTones`; copy `voicings[index]` to `voicing`; append `noteNameForPitch (step.chord, pitch)` to `notes` for each pitch; set cumulative `startBeat` before adding `durationBeats`.

`formatHuman`:

- Header `Progression: ` + whitespace-collapsed text (runs of whitespace → one space, trim); empty → `(empty)`.
- Settings line from `formatSettingsLine`.
- Blank line.
- Diagnostics: `N error(s):` (singular for 1), then `Error at character <offset>: <message>` plus ` (<token>)` when non-empty.
- Else empty chords: `Empty progression (no chords).`
- Else compute column widths from `Chord`, `Degree`, `Notes`, `Beats`, `MIDI` and all row values; left-align, two spaces between columns, right-trim each line; `Notes` joins names with spaces; `MIDI` joins ints with spaces; `Beats` uses `formatNumber`.
- If `midiFile` has a value, append `Wrote <path>`.
- Join lines with `\n` and end with `\n`.

`formatJson`:

- Emit the schema from spec section 4.2. Use two-space indentation for objects; scalar arrays inline with `, ` between values. End with `}\n`.
- `settings` fields use `keyName()`, `modeName()`, `voicingName()`, `rangeName()`, `velocity`, `bpm`.
- `chords` objects: `source`, `symbol`, `degree`, `spelledTones`, `notes`, `voicing`, `startBeat`, `durationBeats`.
- `valid`, `diagnostics` (`offset`, `token`, `message`), `midiFile` (string or null), `error` (string or null).
- Escape strings: `"` → `\"`, `\` → `\\`, backspace/formfeed/newline/carriage-return/tab to their short escapes, bytes below 0x20 → `\u00XX`; pass other bytes through unchanged so UTF-8 survives.
- `formatNumber`: `std::to_chars` with the default general format (shortest round-trip); integral doubles print without a decimal point. Locale-independent.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
./build.sh --test
```

Expected: all three suites pass with 0 failures.

- [ ] **Step 5: Commit**

```bash
git add Source/Cli/CliReport.h Source/Cli/CliReport.cpp Tests/CliTest.cpp CMakeLists.txt
git commit -m "feat: add CLI report formatting for table and JSON"
```

---

### Task 6: Implement the REPL session

**Files:**
- Create: `Source/Cli/CliSession.h`
- Create: `Source/Cli/CliSession.cpp`
- Modify: `CMakeLists.txt` (add `Source/Cli/CliSession.cpp` to `CLI_CORE_SOURCES`)
- Test: `Tests/CliTest.cpp`

**Interfaces:**
- Consumes: `CliSettings`, `buildReport`, `formatHuman`, `formatJson`, `resolveMidiPath`, `chordtext::writeMidiFile`.
- Produces:

```cpp
namespace chordtext::cli
{
struct CliSessionOutput
{
    std::string report;
    std::string feedback;
    bool quit = false;
};

class CliSession
{
public:
    explicit CliSession (CliSettings settings = {});
    CliSessionOutput handleLine (std::string_view line);
    const CliSettings& settings() const;
    const std::optional<std::string>& lastValidText() const;

private:
    CliSettings settings_;
    std::optional<std::string> lastValidText_;
};
}
```

- [ ] **Step 1: Write the failing tests**

Add to `Tests/CliTest.cpp` (before `return reportAndExit();`), and include `"Cli/CliSession.h"`, `<filesystem>`, `<fstream>`:

```cpp
CliSession session;
auto output = session.handleLine ("   ");
CHECK (output.report.empty());
CHECK (output.feedback.empty());
CHECK (! output.quit);

output = session.handleLine ("Cmaj7");
CHECK (output.report.find ("Cmaj7") != std::string::npos);
CHECK (output.feedback.empty());
CHECK (session.lastValidText() == std::optional<std::string> { "Cmaj7" });

output = session.handleLine (":key A");
CHECK (output.report.empty());
CHECK (output.feedback.find ("Key: A major") != std::string::npos);
CHECK (session.settings().keyName() == "A");

session.handleLine (":mode minor");
output = session.handleLine ("i-iv-V");
CHECK (output.report.find ("Am") != std::string::npos);
CHECK (output.report.find ("Dm") != std::string::npos);
CHECK (output.report.find ("E G# B") != std::string::npos);

output = session.handleLine (":velocity 0");
CHECK (output.feedback.find ("Error") != std::string::npos);
CHECK (session.settings().velocity == 100);

output = session.handleLine (":nope");
CHECK (output.feedback.find ("Unknown command") != std::string::npos);

output = session.handleLine (":help");
CHECK (output.feedback.find (":key") != std::string::npos);
CHECK (output.feedback.find (":export") != std::string::npos);

session.handleLine (":json on");
output = session.handleLine ("Cmaj7");
CHECK (output.report.front() == '{');
CHECK (output.report.find ("\"valid\": true") != std::string::npos);
session.handleLine (":json off");

output = session.handleLine (":q");
CHECK (output.quit);

const auto temp = std::filesystem::temp_directory_path()
                      / "chordtext-cli-session-test";
std::filesystem::remove_all (temp);
std::filesystem::create_directories (temp);

CliSession exporter;
output = exporter.handleLine (":export " + (temp / "nothing").string());
CHECK (output.feedback.find ("Nothing to export") != std::string::npos);
CHECK (! std::filesystem::exists (temp / "nothing.mid"));

exporter.handleLine ("Cmaj7");
output = exporter.handleLine (":export " + (temp / "loop").string());
CHECK (output.feedback.find ("Wrote") != std::string::npos);
CHECK (std::filesystem::exists (temp / "loop.mid"));
std::filesystem::remove (temp / "loop.mid");

output = exporter.handleLine ("C99");
CHECK (output.report.find ("Error at character") != std::string::npos);
CHECK (exporter.lastValidText() == std::optional<std::string> { "Cmaj7" });
output = exporter.handleLine (":export " + (temp / "last").string());
CHECK (std::filesystem::exists (temp / "last.mid"));
std::filesystem::remove (temp / "last.mid");

output = exporter.handleLine (":export " + (temp / "out file").string());
CHECK (output.feedback.find ("Wrote") != std::string::npos);
CHECK (std::filesystem::exists (temp / "out file.mid"));
std::filesystem::remove (temp / "out file.mid");

CliSession emptySession;
emptySession.handleLine (",");
output = emptySession.handleLine (":export " + (temp / "empty").string());
CHECK (output.feedback.find ("Cannot write MIDI for an empty progression")
       != std::string::npos);
CHECK (! std::filesystem::exists (temp / "empty.mid"));

const auto blocker = temp / "blocker";
{
    std::ofstream blockerFile (blocker);
    blockerFile << "x";
}
CliSession failing;
failing.handleLine ("C");
output = failing.handleLine (":export " + (blocker / "out").string());
CHECK (output.feedback.find ("Error") != std::string::npos);
CHECK (! std::filesystem::exists (blocker / "out.mid"));

std::filesystem::remove_all (temp);
```

- [ ] **Step 2: Run the tests to verify they fail**

Add `Source/Cli/CliSession.cpp` to `CLI_CORE_SOURCES`, then run:

```bash
./build.sh
./build/Tests/CliTests.exe
```

Expected: compile/link failure for `CliSession`.

- [ ] **Step 3: Implement the session**

Create `CliSession.h` with the exact interface above.

`handleLine`:

- Trim ASCII whitespace from both ends; if empty, return `{}`.
- If the first character is `:`, parse a command: command word is up to the first whitespace; `rest` is the trimmed remainder.
- `:key`, `:mode`, `:voicing`, `:range`, `:velocity`, `:bpm`: parse `rest` with the matching parser. On success update `settings_` and set `feedback = formatSettingsLine (settings_) + "\n"`. On failure set `feedback = "Error: " + <message> + "\n"` and leave settings unchanged. Missing value is an error.
- `:json`: accept `on` or `off` case-insensitively; set `settings_.json`; feedback `JSON mode on\n` / `JSON mode off\n`; otherwise error.
- `:help`: feedback is a multi-line command list that includes `:key`, `:export`, `:quit`, and the current settings line.
- `:quit` and `:q`: set `quit = true`.
- Unknown commands: `Unknown command: <command>\n`.
- `:export <path>`:
  - no `lastValidText_` → feedback `Nothing to export: no valid progression yet.\n`;
  - build a report from `*lastValidText_` with current settings;
  - if the report plan has no events → feedback `Cannot write MIDI for an empty progression.\n`;
  - otherwise `resolveMidiPath (rest)`, call `writeMidiFile (report.plan, path, settings_.midiFileSettings(), error)`. On success feedback `Wrote <path.u8string()>\n`. On failure remove the path with an `std::error_code` and feedback `Error: <error>\n`.
- Any other non-command line: build a report with the original line (`line`), not the trimmed copy, so the JSON `text` stays verbatim; if `valid`, set `lastValidText_` to the original line; set `report` to `settings_.json ? formatJson : formatHuman`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
./build.sh --test
```

Expected: all three suites pass with 0 failures.

- [ ] **Step 5: Commit**

```bash
git add Source/Cli/CliSession.h Source/Cli/CliSession.cpp Tests/CliTest.cpp CMakeLists.txt
git commit -m "feat: add interactive CLI session"
```

---

### Task 7: Add the one-shot runner, main, and executable target

**Files:**
- Create: `Source/Cli/CliRun.h`
- Create: `Source/Cli/CliRun.cpp`
- Create: `Source/Cli/main.cpp`
- Modify: `CMakeLists.txt` (add `CliRun.cpp` to `CLI_CORE_SOURCES`; add the `ChordTextCli` target)
- Test: `Tests/CliTest.cpp`

**Interfaces:**
- Consumes: `CliOptions`, `buildReport`, `formatHuman`, `formatJson`, `resolveMidiPath`, `writeMidiFile`, `CliSession`.
- Produces:

```cpp
namespace chordtext::cli
{
struct CliRunResult
{
    std::string standardOutput;
    std::string standardError;
    int exitCode = 0;
};

CliRunResult runOneShot (std::string_view text, const CliOptions& options);
}
```

- [ ] **Step 1: Write the failing tests**

Add to `Tests/CliTest.cpp` (before `return reportAndExit();`), and include `"Cli/CliRun.h"`:

```cpp
CliOptions oneShot;
auto run = runOneShot ("Cmaj7", oneShot);
CHECK (run.exitCode == 0);
CHECK (run.standardError.empty());
CHECK (run.standardOutput.find ("Cmaj7") != std::string::npos);

run = runOneShot ("C99", oneShot);
CHECK (run.exitCode == 1);
CHECK (run.standardOutput.find ("Error at character") != std::string::npos);

run = runOneShot ("   ", oneShot);
CHECK (run.exitCode == 0);
CHECK (run.standardOutput.find ("Empty progression") != std::string::npos);

oneShot.settings.json = true;
run = runOneShot ("C99", oneShot);
CHECK (run.exitCode == 1);
CHECK (run.standardOutput.find ("\"valid\": false") != std::string::npos);
CHECK (run.standardError.empty());
oneShot.settings.json = false;

const auto runTemp = std::filesystem::temp_directory_path()
                         / "chordtext-cli-run-test";
std::filesystem::remove_all (runTemp);
std::filesystem::create_directories (runTemp);

oneShot.outputPath = (runTemp / "one-shot").string();
run = runOneShot ("C, F, G", oneShot);
CHECK (run.exitCode == 0);
CHECK (run.standardOutput.find ("Wrote ") != std::string::npos);
CHECK (std::filesystem::exists (runTemp / "one-shot.mid"));
std::filesystem::remove (runTemp / "one-shot.mid");

oneShot.outputPath = (runTemp / "empty").string();
run = runOneShot ("   ", oneShot);
CHECK (run.exitCode == 2);
CHECK (run.standardError.find ("Cannot write MIDI for an empty progression")
       != std::string::npos);
CHECK (! std::filesystem::exists (runTemp / "empty.mid"));

oneShot.outputPath = (runTemp / "invalid").string();
run = runOneShot ("C99", oneShot);
CHECK (run.exitCode == 1);
CHECK (! std::filesystem::exists (runTemp / "invalid.mid"));

const auto runBlocker = runTemp / "blocker";
{
    std::ofstream blockerFile (runBlocker);
    blockerFile << "x";
}
oneShot.outputPath = (runBlocker / "out").string();
run = runOneShot ("C", oneShot);
CHECK (run.exitCode == 2);
CHECK (! run.standardError.empty());
CHECK (! std::filesystem::exists (runBlocker / "out.mid"));

CliOptions jsonOptions;
jsonOptions.settings.json = true;
jsonOptions.outputPath = (runTemp / "json").string();
run = runOneShot ("C", jsonOptions);
CHECK (run.exitCode == 0);
CHECK (run.standardError.empty());
CHECK (run.standardOutput.find ("\"midiFile\": \"") != std::string::npos);
CHECK (std::filesystem::exists (runTemp / "json.mid"));

jsonOptions.outputPath = (runBlocker / "out").string();
run = runOneShot ("C", jsonOptions);
CHECK (run.exitCode == 2);
CHECK (run.standardError.empty());
CHECK (run.standardOutput.find ("\"error\": \"") != std::string::npos);

std::filesystem::remove_all (runTemp);
```

- [ ] **Step 2: Run the tests to verify they fail**

Add `Source/Cli/CliRun.cpp` to `CLI_CORE_SOURCES` (without `main.cpp`), then run:

```bash
./build.sh
./build/Tests/CliTests.exe
```

Expected: compile/link failure for `runOneShot`.

- [ ] **Step 3: Implement the one-shot runner**

`runOneShot`:

- `report = buildReport (text, options.settings)`.
- `exitCode = report.valid ? 0 : 1`.
- If `report.valid && options.outputPath.has_value()`:
  - if `report.plan.events.empty()`, set `report.error = "Cannot write MIDI for an empty progression."` and `exitCode = 2`;
  - else resolve the path, call `writeMidiFile (report.plan, path, options.settings.midiFileSettings(), error)`. On success set `report.midiFile = path.u8string()`. On failure remove the partial file with an `std::error_code`, set `report.error = error`, and `exitCode = 2`.
- `standardOutput = options.settings.json ? formatJson (report) : formatHuman (report)`.
- In human mode, if `report.error` has a value, set `standardError = "Error: " + *report.error + "\n"`. In JSON mode leave stderr empty (the error is in the document).

- [ ] **Step 4: Add `main.cpp` and the executable target**

Create `Source/Cli/main.cpp`:

- Include `"Cli/CliOptions.h"`, `"Cli/CliRun.h"`, `"Cli/CliSession.h"`, `"Cli/CliSettings.h"`, `<iostream>`, `<sstream>`, `<string>`, `<vector>`.
- Under `_WIN32`, include `<windows.h>`, `<io.h>`, and `<cstdio>`; define `int wmain (int argc, wchar_t** argv)`.
- Convert each wide argument to UTF-8 with `WideCharToMultiByte (CP_UTF8, 0, ...)`, skipping the program name.
- Call `SetConsoleCP (CP_UTF8)` and `SetConsoleOutputCP (CP_UTF8)`.
- Non-Windows fallback: `int main (int argc, char** argv)` copying the arguments.
- Shared `runApplication (const std::vector<std::string>& args)`:
  - `auto parsed = parseOptions (args);`
  - `help` → `std::cout << helpText(); return 0;`
  - `usageError` → `std::cerr << parsed.error << "\n"; return 2;`
  - Determine `interactive` with `_isatty (_fileno (stdin)) != 0` under `_WIN32`.
  - No progression and interactive:
    - if `outputPath` has a value, print `--out requires a progression or piped input. Use :export in the REPL.` to stderr and return 2;
    - print `Chord Text CLI — :help for commands\n` plus `formatSettingsLine (settings) + "\n\n"` to stdout;
    - loop: print `chord> ` and flush; `std::getline`; break on EOF; call `CliSession::handleLine`; write non-empty `report` to stdout and non-empty `feedback` to stderr, flushing each; break when `quit`;
    - return 0.
  - No progression and non-interactive: read all of `std::cin` through `std::ostringstream`; if `std::cin.bad()`, print `Cannot read standard input.` to stderr and return 2; use the resulting text.
  - Otherwise use `*parsed.options.progression` or the piped text.
  - Call `runOneShot (text, parsed.options)`, write `standardOutput` to stdout and `standardError` to stderr, return `exitCode`.

Add the target to `CMakeLists.txt` after `CliTests`:

```cmake
add_executable(ChordTextCli
    Source/Cli/main.cpp
    ${CLI_CORE_SOURCES}
    ${THEORY_SOURCES})
target_include_directories(ChordTextCli PRIVATE Source)
target_compile_features(ChordTextCli PRIVATE cxx_std_17)
set_target_properties(ChordTextCli PROPERTIES
    OUTPUT_NAME "chordtext-cli"
    RUNTIME_OUTPUT_DIRECTORY                 "${CMAKE_BINARY_DIR}/Cli"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG           "${CMAKE_BINARY_DIR}/Cli"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE         "${CMAKE_BINARY_DIR}/Cli"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL      "${CMAKE_BINARY_DIR}/Cli"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO  "${CMAKE_BINARY_DIR}/Cli")
```

Do not add `main.cpp` to `CLI_CORE_SOURCES`.

- [ ] **Step 5: Run tests and smoke-test the executable**

```bash
./build.sh --test
./build/Cli/chordtext-cli.exe "Cmaj7, Am7, Dm7, G7"
./build/Cli/chordtext-cli.exe --key A --mode minor "i-iv-V"
./build/Cli/chordtext-cli.exe --json "Cmaj7"
./build/Cli/chordtext-cli.exe "C°"
printf "C\r\nAm7\r\n" | ./build/Cli/chordtext-cli.exe
./build/Cli/chordtext-cli.exe "C, H"
echo $?
./build/Cli/chordtext-cli.exe --out build/cli-smoke "C, F, G"
test -f build/cli-smoke.mid && rm build/cli-smoke.mid
./build/Cli/chordtext-cli.exe --help
```

Expected: tables for valid input; `--json` emits a valid document; `C°` shows `Cdim`; CRLF input parses; `C, H` exits 1; the `.mid` is written then removed; help exits 0.

Then run the REPL in a real terminal (not piped):

```bash
./build/Cli/chordtext-cli.exe
```

Enter `Cmaj7`, `:key A`, `i-iv-V`, `:export build/cli-repl-smoke`, and `:q`. Confirm notes update with the key, the export reports `Wrote`, `build/cli-repl-smoke.mid` exists, and remove it.

- [ ] **Step 6: Commit**

```bash
git add Source/Cli/CliRun.h Source/Cli/CliRun.cpp Source/Cli/main.cpp Tests/CliTest.cpp CMakeLists.txt
git commit -m "feat: add chordtext-cli executable"
```

---

### Task 8: Document the utility and run final verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: the finished CLI from Task 7.
- Produces: user-facing documentation; no code interface.

- [ ] **Step 1: Update the README**

Add a `## Command-line utility` section after `## Dragging clips into a DAW` and before `## Tests`, covering:

- the executable path `build/Cli/chordtext-cli.exe`;
- one-shot, piped-stdin, and REPL examples, including `chordtext-cli --key A --mode minor "i-iv-V"` and `echo "C, H, C99" | chordtext-cli`;
- the full option table and defaults from spec section 3.1;
- the human table example from spec section 4.1, with a note that `Notes` follows the voicing and the `MIDI` column shows pitches;
- the diagnostic wording example;
- exit codes 0, 1, and 2;
- REPL commands `:key`, `:mode`, `:voicing`, `:range`, `:velocity`, `:bpm`, `:json on|off`, `:export`, `:help`, `:quit`/`:q`;
- JSON output example and the field meanings, including `spelledTones`, `notes`, `voicing`, `startBeat`, `durationBeats`, `midiFile`, and `error`;
- `--out` / `:export` extension behavior (`.mid` replaces a non-`.mid` extension) and that the CLI uses the current settings to re-parse the last valid text;
- Windows-only console and UTF-8 notes.

Update existing README sections:

- Build outputs: add `build/Cli/chordtext-cli.exe`.
- Tests: mention `CliTests`, the command `./build/Tests/CliTests.exe`, and that the build scripts run all three suites.
- Repository layout: add `Source/Cli/`, `Source/Theory/MidiFileWriter`, and `CliTests`.
- Known limitations: add the six CLI limitations from spec section 8 (Windows-only, no audio preview, line-oriented REPL, default 120 BPM, REPL JSON interleaved with stderr feedback, shared writer parity guarded by read-back tests).

- [ ] **Step 2: Run the complete verification**

```bash
./build.sh --test
```

Expected: the build completes with no project-code warnings and all three executables report 0 failures. Then confirm the documented paths exist:

```bash
test -f "build/ChordText_artefacts/Release/VST3/Chord Text.vst3/Contents/x86_64-win/Chord Text.vst3"
test -f build/Cli/chordtext-cli.exe
test -f build/Tests/TheoryTests.exe
test -f build/Tests/ProcessorTests.exe
test -f build/Tests/CliTests.exe
```

Expected: every `test -f` succeeds.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document the command-line utility"
```
