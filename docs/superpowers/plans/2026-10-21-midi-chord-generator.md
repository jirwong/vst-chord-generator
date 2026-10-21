# MIDI Chord Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a VST3 MIDI generator that parses chord symbols and Roman numerals, plays the progression in sync with the host, exports it as a Standard MIDI File, and shows chord degrees and spelled notes.

**Architecture:** A pure C++ theory library parses text, builds chords, voices them, and produces an immutable `RenderPlan`. A thin JUCE processor schedules the plan against the host transport into a `MidiBuffer`, publishes plans through a triple buffer, and writes MIDI files. A JUCE editor provides text entry, controls, diagnostics, and a learning table.

**Tech Stack:** C++17, JUCE 8.0.15, CMake 3.22+, Visual Studio 17 2022 generator, MSVC x64. No other dependencies.

**Spec:** `docs/superpowers/specs/2026-10-21-midi-chord-generator-design.md`

## Global Constraints

- Platform: Windows x64 only. No macOS/Linux/AU/AAX work.
- C++ standard: C++17. Allowed dependencies: JUCE and the C++ standard library.
- JUCE: 8.0.15, cloned once into `libs/JUCE`, which is git-ignored.
- CMake: minimum 3.22. Generator: `Visual Studio 17 2022`, platform `x64`. Do not use Ninja.
- Plugin shape: VST3 MIDI effect, no audio buses. `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`, `IS_MIDI_EFFECT TRUE`, `IS_SYNTH FALSE`.
- `processBlock` must not allocate, lock, perform file I/O, or destroy objects.
- MIDI channel is 1. Default velocity is 100. Default range is C3–C6 (MIDI 48–84). Default chord duration is 4 beats.
- Stable parameter IDs: `key`, `mode`, `voicing`, `range`, `velocity`, `loop`. Text property key: `progressionText`.
- Chord parsing, theory, and voicing must compile without JUCE.
- No build warnings from code we write, using `juce_recommended_warning_flags`. MSVC compiles with `/utf-8` so `°` and `ø7` literals are reliable.
- JUCE 8 is AGPLv3 or commercial. Personal learning use without distribution is permitted; distribution needs a deliberate licensing decision.

## Review Focus

These are the failure modes most likely to bite a user, and the task that must pin each one:

1. **Stop, seek, bypass, or loop while notes are active** — expected: all generated notes are released, with no stuck notes. Pinned in Task 7.
2. **Editing text, key, or voicing during playback** — expected: old notes are released, the new plan starts cleanly, and invalid text does not replace the last valid plan. Pinned in Task 8.
3. **Minor-key and chromatic spelling** — expected: A minor `V` is E–G#–B, not E–Ab–B; G#maj7 contains F##. Pinned in Tasks 2, 3, and 5.
4. **Chords with different note counts and range edges** — expected: every note is in 0–127, slash bass is lowest, and voice-leading never exceeds root-position movement. Pinned in Task 6.
5. **Loop boundary with repeated identical pitches** — expected: note-offs precede note-ons at the same beat, so repeated chords retrigger without stuck notes. Pinned in Tasks 6, 7, and 8.

---

### Task 1: Buildable MIDI-effect walking skeleton

**Files:**
- Create: `CMakeLists.txt`
- Create: `README.md`
- Create: `build.sh`
- Create: `build.bat`
- Create: `Source/Plugin/PluginProcessor.h`
- Create: `Source/Plugin/PluginProcessor.cpp`
- Create: `Tests/Check.h`
- Create: `Tests/ProcessorTest.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: class `ChordTextAudioProcessor`; the `ProcessorTests` target; the plugin target `ChordText`; `Tests/Check.h`; a hardcoded C-major walking skeleton that later tasks replace.

- [ ] **Step 1: Fetch JUCE 8.0.15 and add build definitions**

```bash
git clone --depth 1 --branch 8.0.15 --recurse-submodules --shallow-submodules https://github.com/juce-framework/JUCE.git libs/JUCE
```

Write `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)

project(ChordText VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at libs/JUCE. See README.md for the clone command.")
endif()

add_subdirectory(libs/JUCE)

set(THEORY_SOURCES)
set(PLUGIN_SOURCES Source/Plugin/PluginProcessor.cpp)

juce_add_plugin(ChordText
    COMPANY_NAME "jirwo"
    PLUGIN_MANUFACTURER_CODE Jirw
    PLUGIN_CODE Chtx
    PRODUCT_NAME "Chord Text"
    FORMATS VST3
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ChordText PRIVATE ${PLUGIN_SOURCES} ${THEORY_SOURCES})
target_include_directories(ChordText PRIVATE Source)
target_compile_definitions(ChordText PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ChordText PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(ProcessorTests PRODUCT_NAME "ProcessorTests")
target_sources(ProcessorTests PRIVATE
    Tests/ProcessorTest.cpp
    ${PLUGIN_SOURCES}
    ${THEORY_SOURCES})
target_include_directories(ProcessorTests PRIVATE Source)
target_compile_definitions(ProcessorTests PRIVATE
    "JucePlugin_Name=\"Chord Text\""
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(ProcessorTests PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(ProcessorTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Tests")
```

Add a minimal `README.md` with the prerequisites, the JUCE clone command, and:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
./build/Tests/ProcessorTests.exe
```

`build.sh` must locate CMake (PATH or the Build Tools copy), configure when `build/CMakeCache.txt` is missing, build Release, and run `./build/Tests/ProcessorTests.exe` when passed `--test`. `build.bat` must do the same with `test` as its argument.

- [ ] **Step 2: Write the failing processor test**

Write `Tests/Check.h` with `CHECK`, `CHECK_NEAR`, counters, and `reportAndExit()` that returns non-zero on failure. Write `Tests/ProcessorTest.cpp`:

```cpp
#include "Check.h"
#include "Plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class FakePlayHead : public juce::AudioPlayHead
{
public:
    void set (double ppqIn, bool playingIn, double bpmIn)
    {
        ppq = ppqIn;
        playing = playingIn;
        bpm = bpmIn;
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        return info;
    }

private:
    double ppq = 0.0;
    bool playing = true;
    double bpm = 120.0;
};

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    FakePlayHead head;
    head.set (0.0, true, 120.0);

    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);

    int noteOns = 0;
    int pitchSum = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            ++noteOns;
            pitchSum += message.getNoteNumber();
        }
    }

    CHECK (noteOns == 3);
    CHECK (pitchSum == 60 + 64 + 67);

    return reportAndExit();
}
```

- [ ] **Step 3: Run the build to verify it fails**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target ProcessorTests
```

Expected: compile error because `Plugin/PluginProcessor.h` does not exist.

- [ ] **Step 4: Implement the walking-skeleton processor**

Create `Source/Plugin/PluginProcessor.h` declaring `ChordTextAudioProcessor : public juce::AudioProcessor` with the standard overrides:

```cpp
void prepareToPlay (double, int) override;
void releaseResources() override;
void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
juce::AudioProcessorEditor* createEditor() override;
bool hasEditor() const override;
const juce::String getName() const override;
bool acceptsMidi() const override;
bool producesMidi() const override;
bool isMidiEffect() const override;
double getTailLengthSeconds() const override;
int getNumPrograms() override;
int getCurrentProgram() override;
void setCurrentProgram (int) override;
const juce::String getProgramName (int) override;
void changeProgramName (int, const juce::String&) override;
void getStateInformation (juce::MemoryBlock&) override;
void setStateInformation (const void*, int) override;
```

`createEditor()` returns `nullptr` in this task. `getTailLengthSeconds()` returns `0.0`. `processBlock` clears the incoming `MidiBuffer`, reads `getPlayHead()->getPosition()`, and, when playing, emits note-ons for MIDI pitches 60, 64, and 67 at sample 0 on every 4-beat boundary. This is deliberately temporary; Task 7 replaces it with the parsed plan.

- [ ] **Step 5: Build and run the test**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: `3 checks, 0 failures`, exit code 0.

- [ ] **Step 6: Verify the walking skeleton in FL Studio**

Build the plugin, add the VST3 output folder as a search path in FL Studio, load `Chord Text` as a generator, and route it to an instrument (Patcher or a MIDI-out route). Press play and confirm the instrument receives C–E–G.

This step is a gate. If FL Studio cannot route a VST3 MIDI effect to another instrument, stop and report it; the fallback is an instrument/audio-effect shape with MIDI output, and the theory core is unchanged.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt README.md build.sh build.bat Source Tests
git commit -m "Add buildable MIDI-effect walking skeleton"
```

---

### Task 2: Spelled pitches and keys

**Files:**
- Create: `Source/Theory/Note.h`
- Create: `Source/Theory/Note.cpp`
- Create: `Source/Theory/Key.h`
- Create: `Source/Theory/Key.cpp`
- Create: `Tests/TheoryTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Tests/Check.h`.
- Produces: `chordtext::PitchClass`, `chordtext::Note`, `chordtext::Mode`, `chordtext::Key`.

```cpp
namespace chordtext
{
struct PitchClass
{
    char letter = 'C';
    int alteration = 0;
    int semitone() const;
    std::string name() const;
};

struct Note
{
    PitchClass pitchClass;
    int octave = 4;
    int midi() const;
    std::string name() const;
};

enum class Mode { major, minor };

class Key
{
public:
    Key (PitchClass tonic, Mode mode);
    PitchClass tonic() const;
    Mode mode() const;
    PitchClass degree (int number) const;   // number is 1..7
    std::string name() const;

private:
    PitchClass tonic_;
    Mode mode_;
};
}
```

- [ ] **Step 1: Add the theory test target**

Add `Tests/TheoryTest.cpp` and `Source/Theory/Note.cpp`/`Key.cpp` to `THEORY_SOURCES`. Add a plain `TheoryTests` executable with `Tests/TheoryTest.cpp`, `THEORY_SOURCES`, include directory `Source`, `cxx_std_17`, and the same `build/Tests` output properties as `ProcessorTests`.

- [ ] **Step 2: Write the failing tests**

In `Tests/TheoryTest.cpp`:

```cpp
using namespace chordtext;

CHECK (PitchClass {'C', 0}.semitone() == 0);
CHECK (PitchClass {'C', 1}.name() == "C#");
CHECK (PitchClass {'G', 2}.name() == "G##");
CHECK (Note {{'C', 0}, 4}.midi() == 60);
CHECK (Note {{'B', 0}, 3}.midi() == 59);

CHECK (Key {{'C', 0}, Mode::major}.degree (1).name() == "C");
CHECK (Key {{'C', 0}, Mode::major}.degree (7).name() == "B");
CHECK (Key {{'G', 1}, Mode::major}.degree (7).name() == "F##");
CHECK (Key {{'A', 0}, Mode::minor}.degree (3).name() == "C");
CHECK (Key {{'A', 0}, Mode::minor}.degree (7).name() == "G");
CHECK (Key {{'E', -1}, Mode::major}.degree (4).name() == "Ab");
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target TheoryTests
./build/Tests/TheoryTests.exe
```

Expected: compile failure because the theory headers do not exist.

- [ ] **Step 4: Implement `PitchClass`, `Note`, and `Key`**

`PitchClass::semitone()` maps A–G to 0–11 and adds `alteration`. `PitchClass::name()` appends `#` or `b` repeated `abs(alteration)` times, in the correct direction. `Note::midi()` uses `12 * (octave + 1) + semitone()`; `Note::name()` appends the octave.

`Key::degree` uses major intervals `{0,2,4,5,7,9,11}` and natural-minor intervals `{0,2,3,5,7,8,10}`. It spells by letter, not by choosing a sharp/flat spelling from a pitch-class table. For example, G# major's seventh must be F##.

- [ ] **Step 5: Run the tests to verify they pass**

Same command as Step 3. Expected: `0 failures`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt Source/Theory Tests/TheoryTest.cpp
git commit -m "Add spelled pitches and keys"
```

---

### Task 3: Chord model, spelling, and degree labels

**Files:**
- Create: `Source/Theory/Chord.h`
- Create: `Source/Theory/Chord.cpp`
- Modify: `Tests/TheoryTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PitchClass`, `Note`, `Key`.
- Produces: `chordtext::ChordSpec`, `chordtext::Chord`, `chordtext::degreeLabel`.

```cpp
struct ChordSpec
{
    PitchClass root;
    std::string quality;   // canonical quality token
    std::vector<std::pair<int, int>> alterations; // {degree, semitoneDelta}
    std::optional<PitchClass> slashBass;
    std::string sourceText;
};

class Chord
{
public:
    explicit Chord (ChordSpec spec);
    const ChordSpec& spec() const;
    const std::vector<PitchClass>& tones() const;
    std::string symbol() const;
};

std::string degreeLabel (const Chord& chord, const Key& key);
```

Canonical qualities and intervals:

| Quality | Intervals |
|---|---|
| `""` | 0, 4, 7 |
| `m` | 0, 3, 7 |
| `dim` | 0, 3, 6 |
| `aug` | 0, 4, 8 |
| `sus2` | 0, 2, 7 |
| `sus4` | 0, 5, 7 |
| `5` | 0, 7 |
| `6` | 0, 4, 7, 9 |
| `m6` | 0, 3, 7, 9 |
| `7` | 0, 4, 7, 10 |
| `maj7` | 0, 4, 7, 11 |
| `m7` | 0, 3, 7, 10 |
| `mMaj7` | 0, 3, 7, 11 |
| `dim7` | 0, 3, 6, 9 |
| `m7b5` | 0, 3, 6, 10 |
| `7sus4` | 0, 5, 7, 10 |
| `9` | 0, 4, 7, 10, 14 |
| `maj9` | 0, 4, 7, 11, 14 |
| `m9` | 0, 3, 7, 10, 14 |
| `add9` | 0, 4, 7, 14 |
| `9sus4` | 0, 5, 7, 10, 14 |

Spell each interval by letter offset: root 0, second/ninth +1 letter, third +2, fourth +3, fifth +4, sixth +5, seventh +6. Choose the accidental that makes the letter's pitch match the interval. Double accidentals are allowed.

`Chord::symbol()` rebuilds a canonical symbol such as `Cmaj7`, `Cm7b5`, `C7b9`, `C/E`, and `C9sus4`.

`degreeLabel` uses the key to label the chord root. Uppercase numerals are major/dominant; lowercase are minor/diminished. Suffix examples: `Imaj7`, `vi7`, `ii7`, `V7`, `i`, `bVII`, `viio7`, `IVmaj7`. Chromatic labels are best-effort; exact chromatic degree labels are not a success criterion.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/TheoryTest.cpp`:

```cpp
using namespace chordtext;

auto chord = [] (PitchClass root, std::string quality = "",
                 std::vector<std::pair<int,int>> alterations = {},
                 std::optional<PitchClass> bass = std::nullopt)
{
    return Chord (ChordSpec { root, std::move (quality), std::move (alterations), bass, "" });
};

auto names = [] (const Chord& c)
{
    std::string result;
    for (const auto& tone : c.tones())
        result += tone.name();
    return result;
};

CHECK (names (chord ({'C', 0}, "maj7")) == "CEGB");
CHECK (names (chord ({'C', 0}, "maj9")) == "CEGBD");
CHECK (names (chord ({'C', 0}, "6")) == "CEGA");
CHECK (names (chord ({'C', 0}, "add9")) == "CEGD");
CHECK (names (chord ({'C', 0}, "m7b5")) == "CEbGbBb");
CHECK (names (chord ({'C', 0}, "dim7")) == "CEbGbBbb");
CHECK (names (chord ({'G', 1}, "maj7")) == "G#B#D#F##");
CHECK (names (chord ({'C', 0}, "7sus4")) == "CFGBb");
CHECK (names (chord ({'C', 0}, "9sus4")) == "CFGBbD");
CHECK (names (chord ({'C', 0}, "7", {{9, -1}})) == "CEGBbDb");
CHECK (names (chord ({'C', 1}, "")) == "C#E#G#");
CHECK (chord ({'C', 1}, "").symbol() == "C#");
CHECK (chord ({'C', 0}, "7", {{9, -1}}).symbol() == "C7b9");
CHECK (chord ({'C', 0}, "", {}, PitchClass {'E', 0}).symbol() == "C/E");

CHECK (degreeLabel (chord ({'C', 0}, "maj7"), Key {{'C', 0}, Mode::major}) == "Imaj7");
CHECK (degreeLabel (chord ({'A', 0}, "m7"), Key {{'C', 0}, Mode::major}) == "vi7");
CHECK (degreeLabel (chord ({'C', 0}, "m"), Key {{'C', 0}, Mode::major}) == "i");
CHECK (degreeLabel (chord ({'B', -1}, ""), Key {{'C', 0}, Mode::major}) == "bVII");
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target TheoryTests
```

Expected: compile failure because `Chord.h` does not exist.

- [ ] **Step 3: Implement the chord model and degree labels**

Implement the interval table, letter-based spelling, alteration handling, `symbol()`, and `degreeLabel`. Alterations `{5,-1}`, `{5,+1}`, `{9,-1}`, `{9,+1}` map to `b5`, `#5`, `b9`, `#9`. A slash bass does not change `tones()`; it is used by the voicer.

- [ ] **Step 4: Run the tests to verify they pass**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Theory Tests/TheoryTest.cpp
git commit -m "Add chord model, spelling, and degree labels"
```

---

### Task 4: Absolute chord-symbol parser

**Files:**
- Create: `Source/Theory/Diagnostic.h`
- Create: `Source/Theory/ChordSymbolParser.h`
- Create: `Source/Theory/ChordSymbolParser.cpp`
- Modify: `Tests/TheoryTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ChordSpec`, `PitchClass`, `Chord`.
- Produces: `chordtext::Diagnostic`, `chordtext::ChordSymbolParseResult`, `parseChordSymbol`.

```cpp
struct Diagnostic
{
    std::string token;
    std::size_t offset = 0;
    std::string message;
};

struct ChordSymbolParseResult
{
    std::optional<ChordSpec> chord;
    std::optional<Diagnostic> error;
};

ChordSymbolParseResult parseChordSymbol (std::string_view token, std::size_t offset);
```

Accepted grammar: root `[A-G]` with optional single `#` or `b`; a quality from the Task 3 table or an alias; zero or more `b5`/`#5`/`b9`/`#9` alterations; optional `/bass` with a single accidental. Quality aliases: `min`→`m`, `o`/`°`→`dim`, `+`→`aug`, `M7`→`maj7`, `min7`→`m7`, `minMaj7`→`mMaj7`, `o7`/`°7`→`dim7`, `ø7`→`m7b5`, `M9`→`maj9`, `min9`→`m9`, `min6`→`m6`.

- [ ] **Step 1: Write the failing tests**

```cpp
auto parse = [] (std::string token)
{
    return parseChordSymbol (token, 7);
};

CHECK (parse ("Cmaj7").chord->quality == "maj7");
CHECK (parse ("CM7").chord->quality == "maj7");
CHECK (parse ("Emin9").chord->quality == "m9");
CHECK (parse ("Cm7b5").chord->quality == "m7b5");
CHECK (parse ("Cø7").chord->quality == "m7b5");
CHECK (parse ("C7b9").chord->alterations == std::vector<std::pair<int,int>> {{9, -1}});
CHECK (parse ("C/E").chord->slashBass->name() == "E");
CHECK (parse ("F#m7b5/C").chord->slashBass->name() == "C");
CHECK (! parse ("C99").chord.has_value());
CHECK (! parse ("C11").chord.has_value());
CHECK (! parse ("C13").chord.has_value());
CHECK (! parse ("C6/9").chord.has_value());
CHECK (! parse ("H").chord.has_value());
CHECK (parse ("H").error->offset == 7);
CHECK (parse ("C99").error->message.find ("unknown extension") != std::string::npos);
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target TheoryTests
./build/Tests/TheoryTests.exe
```

Expected: compile failure because `ChordSymbolParser.h` does not exist.

- [ ] **Step 3: Implement `parseChordSymbol`**

Parse longest-match quality tokens first. Reject unsupported extensions and double accidentals rather than guessing. Fill `Diagnostic.token`, `Diagnostic.offset`, and a message such as `unknown extension "99"`.

- [ ] **Step 4: Run the tests to verify they pass**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Theory Tests/TheoryTest.cpp
git commit -m "Add absolute chord-symbol parser"
```

---

### Task 5: Roman numerals and progression parsing

**Files:**
- Create: `Source/Theory/RomanNumeralParser.h`
- Create: `Source/Theory/RomanNumeralParser.cpp`
- Create: `Source/Theory/Progression.h`
- Create: `Source/Theory/Progression.cpp`
- Modify: `Tests/TheoryTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Key`, `ChordSpec`, `Diagnostic`, `parseChordSymbol`.
- Produces: `RomanNumeralParseResult`, `parseRomanNumeral`, `ProgressionStep`, `Progression`, `ProgressionParseResult`, `parseProgression`.

```cpp
struct RomanNumeralParseResult
{
    std::optional<ChordSpec> chord;
    std::optional<Diagnostic> error;
};

RomanNumeralParseResult parseRomanNumeral (std::string_view token,
                                           const Key& key,
                                           std::size_t offset);

struct ProgressionStep
{
    Chord chord;
    double durationBeats = 4.0;
    std::string sourceText;
    std::size_t offset = 0;
};

struct Progression
{
    std::vector<ProgressionStep> steps;
    double totalBeats() const;
};

struct ProgressionParseResult
{
    std::optional<Progression> progression;
    std::vector<Diagnostic> diagnostics;
};

ProgressionParseResult parseProgression (std::string_view text, const Key& key);
```

Progression tokenizer rules:

- Separators: comma, whitespace, newline, `|`, and `-`. `|` and `-` are visual only. A hyphen is not a minor-quality alias.
- A token may end with `:N`, where `N` is a positive finite double.
- Empty text is a valid empty progression.
- If any token is invalid, `progression` is empty and all diagnostics are returned.
- Detection: a token starting with `I`/`i`/`V`/`v`, or with `b`/`#` followed by one of those, is Roman. A token starting with `A`–`G` is absolute. Anything else is an error.
- Roman input cannot use slash bass.

- [ ] **Step 1: Write the failing tests**

```cpp
const Key cMajor {{'C', 0}, Mode::major};
const Key aMinor {{'A', 0}, Mode::minor};

auto progression = [] (std::string text, const Key& key)
{
    return parseProgression (text, key);
};

auto symbols = [] (const Progression& p)
{
    std::string result;
    for (const auto& step : p.steps)
        result += step.chord.symbol() + "|";
    return result;
};

CHECK (symbols (*progression ("C, Cmaj7, Dmaj7, Emin9, Fmaj7", cMajor).progression)
       == "C|Cmaj7|Dmaj7|Em9|Fmaj7|");
CHECK (symbols (*progression ("I-IV-V", cMajor).progression) == "C|F|G|");
CHECK (symbols (*progression ("i-iv-V", aMinor).progression) == "Am|Dm|E|");
CHECK (symbols (*progression ("bVII, #iv", cMajor).progression) == "Bb|F#m|");
CHECK (progression ("Cmaj7:2, G7", cMajor).progression->totalBeats() == 6.0);
CHECK (progression ("C | Am | F | G", cMajor).progression->steps.size() == 4);
CHECK (! progression ("C, H, C99", cMajor).progression.has_value());
CHECK (progression ("C, H, C99", cMajor).diagnostics.size() == 2);
CHECK (progression ("", cMajor).progression->steps.empty());
CHECK (progression ("", cMajor).diagnostics.empty());
CHECK (progression ("Cmaj7:0", cMajor).diagnostics.size() == 1);
CHECK (progression ("Cmaj7:nan", cMajor).diagnostics.size() == 1);
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target TheoryTests
./build/Tests/TheoryTests.exe
```

Expected: compile failure because `Progression.h` does not exist.

- [ ] **Step 3: Implement Roman numerals and progression parsing**

`parseRomanNumeral` reads optional accidentals, a numeral, optional quality, and alterations. Resolve the numeral through `Key::degree`, apply accidentals, set the default quality from case, then apply any explicit quality. `parseProgression` handles tokenization, duration splitting, absolute-vs-Roman detection, and diagnostic offsets.

- [ ] **Step 4: Run the tests to verify they pass**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Theory Tests/TheoryTest.cpp
git commit -m "Add Roman numerals and progression parsing"
```

---

### Task 6: Voicer and render plan

**Files:**
- Create: `Source/Theory/Voicer.h`
- Create: `Source/Theory/Voicer.cpp`
- Create: `Source/Theory/RenderPlan.h`
- Create: `Source/Theory/RenderPlan.cpp`
- Modify: `Tests/TheoryTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Chord`, `ChordSpec`, `Progression`.
- Produces: `VoicingMode`, `VoicingSettings`, `voiceChord`, `voiceProgression`, `MidiEvent`, `RenderPlan`, `buildRenderPlan`.

```cpp
enum class VoicingMode { voiceLed, rootPosition };

struct VoicingSettings
{
    VoicingMode mode = VoicingMode::voiceLed;
    int lowMidi = 48;
    int highMidi = 84;
};

std::vector<int> voiceChord (const Chord& chord,
                             const std::optional<std::vector<int>>& previous,
                             const VoicingSettings& settings);

std::vector<std::vector<int>> voiceProgression (const Progression& progression,
                                                 const VoicingSettings& settings);

struct MidiEvent
{
    double beat = 0.0;
    int pitch = 0;
    int velocity = 100;
    int channel = 1;
    bool noteOn = true;
    int stepIndex = 0;
};

struct RenderPlan
{
    std::vector<MidiEvent> events;
    double lengthBeats = 0.0;
};

RenderPlan buildRenderPlan (const Progression& progression,
                            const std::vector<std::vector<int>>& voicings,
                            int velocity);
```

Voicing rules:

- Root position: root at the lowest octave that fits; stack chord tones above it. Slash bass is the lowest note, with upper voices above it.
- Voice-led: first chord is root position. Later candidates are inversions/octave placements within range. Sort each candidate and the previous voicing ascending; pair the lowest `min(n, m)` notes; minimize the sum of absolute semitone distances. Ties prefer the smaller maximum leap, then the lower average pitch.
- Every pitch must be in `[0, 127]` and within the requested range after octave adjustment.
- Build events with note-on at `step` start and note-off at `step + duration`. Sort by beat, then note-off before note-on, then pitch. `lengthBeats` is the progression total.

- [ ] **Step 1: Write the failing tests**

```cpp
auto chord = [] (PitchClass root, std::string quality = "",
                 std::vector<std::pair<int,int>> alterations = {},
                 std::optional<PitchClass> bass = std::nullopt)
{
    return Chord (ChordSpec { root, std::move (quality), std::move (alterations), bass, "" });
};

const VoicingSettings rootPosition {VoicingMode::rootPosition, 48, 84};
const VoicingSettings voiceLed {VoicingMode::voiceLed, 48, 84};

CHECK (voiceChord (chord ({'C', 0}, "maj7"), std::nullopt, rootPosition)
       == std::vector<int> {48, 52, 55, 59});
CHECK (voiceChord (chord ({'C', 0}, "", {}, PitchClass {'E', 0}), std::nullopt, rootPosition)
       == std::vector<int> {52, 60, 64, 67});
CHECK (voiceChord (chord ({'G', 0}, "7"), std::vector<int> {60, 64, 67, 71}, voiceLed)
       == std::vector<int> {62, 65, 67, 71});
CHECK (voiceChord (chord ({'G', 0}, "7"), std::vector<int> {60, 64, 67, 71}, voiceLed)
       != voiceChord (chord ({'G', 0}, "7"), std::vector<int> {60, 64, 67, 71}, rootPosition));

const Key cMajor {{'C', 0}, Mode::major};
Progression p;
p.steps.push_back ({chord ({'C', 0}, "maj7"), 4.0, "Cmaj7", 0});
p.steps.push_back ({chord ({'G', 0}, "7"), 4.0, "G7", 0});

auto rootVoicings = voiceProgression (p, rootPosition);
auto ledVoicings = voiceProgression (p, voiceLed);
CHECK (rootVoicings == std::vector<std::vector<int>> {{48,52,55,59}, {55,59,62,65}});
CHECK (ledVoicings == rootVoicings);

const auto plan = buildRenderPlan (p, rootVoicings, 100);
CHECK (plan.lengthBeats == 8.0);
CHECK (plan.events.size() == 16);
CHECK (plan.events.front().beat == 0.0);
CHECK (plan.events.front().noteOn);
CHECK (! plan.events[4].noteOn);          // first event at beat 4 is a note-off
CHECK (plan.events[4].beat == 4.0);
CHECK (plan.events[8].noteOn);            // note-ons follow the note-offs
CHECK (plan.events[8].beat == 4.0);
CHECK (plan.events.back().beat == 8.0);
CHECK (! plan.events.back().noteOn);
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target TheoryTests
./build/Tests/TheoryTests.exe
```

Expected: compile failure because `Voicer.h` does not exist.

- [ ] **Step 3: Implement the voicer and render plan**

Implement root-position placement, slash bass, candidate generation, deterministic voice-leading scoring, range enforcement, event construction, and sorting.

- [ ] **Step 4: Run the tests to verify they pass**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Theory Tests/TheoryTest.cpp
git commit -m "Add voicer and render plan"
```

---

### Task 7: Live transport and note lifecycle

**Files:**
- Modify: `Source/Plugin/PluginProcessor.h`
- Modify: `Source/Plugin/PluginProcessor.cpp`
- Modify: `Tests/ProcessorTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `parseProgression`, `voiceProgression`, `buildRenderPlan`, `RenderPlan`, `MidiEvent`.
- Produces: default plan `"Cmaj7, Am7, Dm7, G7"` in C major, voice-led, range C3–C6, velocity 100; transport scheduling; note lifecycle; triple-buffered plan publication; `bool isTransportAvailable() const`; bypass-safe note release.

- [ ] **Step 1: Write the failing transport tests**

Add helpers to `Tests/ProcessorTest.cpp`:

```cpp
static std::vector<int> noteOnPitches (const juce::MidiBuffer& midi)
{
    std::vector<int> result;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            result.push_back (metadata.getMessage().getNoteNumber());
    return result;
}

static int countNoteOffs (const juce::MidiBuffer& midi)
{
    int result = 0;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOff())
            ++result;
    return result;
}
```

Tests:

```cpp
// first chord at transport start
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);

    CHECK (noteOnPitches (midi) == std::vector<int> {48, 52, 55, 59});
}

// stop releases active notes
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);
    CHECK (noteOnPitches (midi).size() == 4);

    head.set (0.02, false, 120.0);
    juce::MidiBuffer stopped;
    p.processBlock (audio, stopped);
    CHECK (countNoteOffs (stopped) == 4);
    CHECK (noteOnPitches (stopped).empty());
}

// bypass releases active notes
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);

    juce::MidiBuffer bypassed;
    p.processBlockBypassed (audio, bypassed);
    CHECK (countNoteOffs (bypassed) == 4);
}

// seek releases old notes and retriggers the active chord
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer first;
    p.processBlock (audio, first);

    head.set (8.0, true, 120.0);
    juce::MidiBuffer sought;
    p.processBlock (audio, sought);
    CHECK (countNoteOffs (sought) >= 4);
    CHECK (! noteOnPitches (sought).empty());
}
```

Also add a loop-boundary test at `ppq 3.99` with a 512-sample block:

```cpp
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    const double blockBeats = 512.0 * 120.0 / (60.0 * 48000.0);

    // Advance without a seek until the block that contains beat 4.
    for (int i = 0; i < 187; ++i)
    {
        head.set (i * blockBeats, true, 120.0);
        juce::MidiBuffer discarded;
        p.processBlock (audio, discarded);
    }

    head.set (187 * blockBeats, true, 120.0);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);

    int lastOffIndex = -1, firstOnIndex = -1, index = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOff()) lastOffIndex = index;
        if (message.isNoteOn() && firstOnIndex < 0) firstOnIndex = index;
        ++index;
    }

    CHECK (lastOffIndex >= 0);
    CHECK (firstOnIndex >= 0);
    CHECK (lastOffIndex < firstOnIndex);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: the first-chord assertions fail because the processor still emits the hardcoded C-major triad at pitch 60, 64, 67.

- [ ] **Step 3: Implement live scheduling and note lifecycle**

In the processor, own three preallocated `RenderPlan` slots plus `std::atomic<int> publishedPlan` and `std::atomic<int> observedPlan`. At the start of `processBlock`, store the published index into `observedPlan`, load that plan, and schedule from it. The message thread writes a slot that is neither published nor observed.

`processBlock` algorithm:

1. Clear the incoming `MidiBuffer`.
2. If no playhead, no PPQ, no BPM, or not playing: release active notes at sample 0; return.
3. Compute `blockStartBeat` and `blockBeats` from PPQ and BPM.
4. If the current beat differs from the expected continuation by more than 0.25 beats, treat it as a seek: release active notes, clear active state, and resync.
5. If the block starts inside a chord, emit that chord's note-ons at sample 0.
6. Compute `blockEndBeat`. If looping and `blockEndBeat` crosses the loop length, schedule the first range up to the loop end, release active notes, then schedule the remainder from beat 0.
7. Otherwise schedule all plan events in `[blockStartBeat, blockEndBeat)`, converting beats to sample offsets.
8. Track active pitches in a fixed `std::array<bool, 128>`.
9. On loop wrap or a loop-off end, release active notes.
10. Update `std::atomic<int> currentStep` and `std::atomic<bool> transportAvailable` for the editor.

No plan slot may be written on the audio thread. The default plan is built once on the message thread before playback. `isTransportAvailable()` returns the last observed availability and is safe to call from the editor timer. Override `processBlockBypassed` to clear the MIDI buffer, release all active notes, and produce no generated notes.

- [ ] **Step 4: Run the tests to verify they pass**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Plugin Tests/ProcessorTest.cpp
git commit -m "Add live transport and note lifecycle"
```

---

### Task 8: Parameters, state, and plan rebuilds

**Files:**
- Modify: `Source/Plugin/PluginProcessor.h`
- Modify: `Source/Plugin/PluginProcessor.cpp`
- Modify: `Tests/ProcessorTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 7 processor.
- Produces: parameter layout, setters/getters, `LearningRow`, editor snapshot, diagnostics, state save/load, plan rebuild on parameter or text change.

```cpp
struct LearningRow
{
    juce::String chord;
    juce::String degree;
    juce::String notes;
};

juce::AudioProcessorValueTreeState apvts;

void setProgressionText (juce::String text);
juce::String getProgressionText() const;
std::vector<chordtext::Diagnostic> getDiagnostics() const;
std::vector<LearningRow> getLearningRows() const;
int getCurrentStep() const;
void rebuildPlan();   // message thread only
```

Parameter layout:

| ID | Type | Choices / range | Default |
|---|---|---|---|
| `key` | `AudioParameterChoice` | `C, C#, Db, D, D#, Eb, E, F, F#, Gb, G, G#, Ab, A, A#, Bb, B` | `C` |
| `mode` | `AudioParameterChoice` | `Major, Minor` | `Major` |
| `voicing` | `AudioParameterChoice` | `Voice-led, Root position` | `Voice-led` |
| `range` | `AudioParameterChoice` | `C2-C5, C3-C6, C4-C7` | `C3-C6` |
| `velocity` | `AudioParameterInt` | 1–127 | 100 |
| `loop` | `AudioParameterBool` | — | `true` |

Text is stored in `apvts.state` as the `progressionText` property. `getStateInformation` copies the APVTS state and writes that property; `setStateInformation` restores it and calls `rebuildPlan()`.

`rebuildPlan()` parses the text with the selected key, voices it with the selected mode/range, builds a plan, and publishes it. If parsing fails, it keeps the previous plan and updates diagnostics/rows from the last valid result. Parameter changes trigger `rebuildPlan()` through `juce::AsyncUpdater`, so the audio thread never parses. Remove all APVTS listeners in the destructor.

- [ ] **Step 1: Write the failing parameter and state tests**

```cpp
// key change updates the parsed rows and generated pitches
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.setProgressionText ("i, V");
    p.apvts.getParameter ("key")->setValueNotifyingHost (13.0f / 16.0f); // A
    p.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);        // Minor
    p.rebuildPlan();
    p.prepareToPlay (48000.0, 512);

    CHECK (p.getLearningRows().size() == 2);
    CHECK (p.getLearningRows()[0].chord == "Am");
    CHECK (p.getLearningRows()[1].chord == "E");

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);
    CHECK (noteOnPitches (midi) == std::vector<int> {57, 60, 64});
}

// invalid text keeps the last valid plan and reports diagnostics
{
    ChordTextAudioProcessor p;
    p.setProgressionText ("C");
    CHECK (p.getLearningRows().size() == 1);
    p.setProgressionText ("H");
    CHECK (p.getLearningRows().size() == 1);
    CHECK (! p.getDiagnostics().empty());
}

// state round-trip restores text and key
{
    ChordTextAudioProcessor a;
    a.setProgressionText ("i, V");
    a.apvts.getParameter ("key")->setValueNotifyingHost (13.0f / 16.0f); // A
    a.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);        // Minor
    juce::MemoryBlock state;
    a.getStateInformation (state);

    ChordTextAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getProgressionText() == "i, V");
    CHECK (b.getLearningRows().size() == 2);
}
```

Also test plan swap while playing: start with `"Cmaj7"`, process a block, set `"G7"`, process again, and assert old notes are released before the new chord is triggered. Test voicing and range changes update the generated pitch set. Test velocity and loop values survive a state round-trip. Test that `loop` set to false stops generating after the progression end.

Add a loop-wrap test with `"C:0.5, G:0.5"` (loop length 1 beat). Advance sequentially in 512-sample blocks at 120 BPM until a block crosses beat 1.0, and assert the wrap block contains note-offs before note-ons. Then stop transport and assert the stop block releases exactly the notes that were active after the wrap, with no duplicates.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: compile failure or failing assertions because the parameters and text API do not exist.

- [ ] **Step 3: Implement parameters, state, and plan rebuilds**

Add `juce::AudioProcessorValueTreeState apvts`, the parameter layout, the text property, the setters/getters, the editor snapshot, and the async rebuild path. Use `apvts.addParameterListener` for `key`, `mode`, `voicing`, `range`, and rebuild through `AsyncUpdater`. `setProgressionText` rebuilds synchronously on the message thread for tests and editor use.

- [ ] **Step 4: Run the tests to verify they pass**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Plugin Tests/ProcessorTest.cpp
git commit -m "Add parameters, state, and plan rebuilds"
```

---

### Task 9: MIDI file export

**Files:**
- Modify: `Source/Plugin/PluginProcessor.h`
- Modify: `Source/Plugin/PluginProcessor.cpp`
- Modify: `Tests/ProcessorTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the current `RenderPlan` and transport BPM.
- Produces: `bool exportMidiFile (const juce::File& file, juce::String& errorMessage);`

- [ ] **Step 1: Write the failing export test**

```cpp
// export writes a readable file with tempo and paired notes
{
    FakePlayHead head;
    head.set (0.0, true, 120.0);
    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.setProgressionText ("Cmaj7, G7");
    p.prepareToPlay (48000.0, 512);

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("chordtext-test", ".mid");
    juce::String error;
    CHECK (p.exportMidiFile (file, error));
    CHECK (error.isEmpty());

    juce::FileInputStream in (file);
    CHECK (in.openedOk());
    juce::MidiFile midiFile;
    CHECK (midiFile.readFrom (in));
    CHECK (midiFile.getTicksPerQuarterNote() == 960);
    CHECK (midiFile.getNumTracks() >= 1);

    int noteOns = 0, noteOffs = 0, tempoEvents = 0;
    std::vector<double> noteOnTicks;
    for (int t = 0; t < midiFile.getNumTracks(); ++t)
    {
        const auto* track = midiFile.getTrack (t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto* event = track->getEventPointer (i);
            const auto message = event->message;
            if (message.isNoteOn())
            {
                ++noteOns;
                noteOnTicks.push_back (event->time);
            }
            if (message.isNoteOff()) ++noteOffs;
            if (message.isTempoMetaEvent())
            {
                ++tempoEvents;
                CHECK_NEAR (message.getTempoSecondsPerQuarterNote(), 0.5, 0.001);
            }
        }
    }

    CHECK (noteOns == 8);
    CHECK (noteOffs == 8);
    CHECK (tempoEvents >= 1);
    CHECK (noteOnTicks.size() == 8);
    for (int i = 0; i < 4; ++i)
        CHECK (noteOnTicks[(size_t) i] == 0.0);
    for (int i = 4; i < 8; ++i)
        CHECK (noteOnTicks[(size_t) i] == 4.0 * 960.0);

    file.deleteFile();
}
```

Also test that export is rejected for invalid text and for an empty plan.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: compile failure because `exportMidiFile` does not exist.

- [ ] **Step 3: Implement `exportMidiFile`**

Build a `juce::MidiFile` with 960 ticks per quarter note. Add a `juce::MidiMessageSequence` containing a track-name event, a tempo event at tick 0 (`round(60,000,000 / bpm)` microseconds per quarter), a 4/4 time-signature event, and every plan event at `beat * 960.0`. Sort and add note-offs before note-ons at equal ticks. Return false with a message when the current diagnostics are non-empty, when the plan has no notes, or when the output stream cannot open.

The tempo is the current playhead BPM; if no playhead tempo is available, use 120.

- [ ] **Step 4: Run the test to verify it passes**

Same command as Step 2. Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt Source/Plugin Tests/ProcessorTest.cpp
git commit -m "Add deterministic MIDI file export"
```

---

### Task 10: Editor and learning table

**Files:**
- Create: `Source/Plugin/PluginEditor.h`
- Create: `Source/Plugin/PluginEditor.cpp`
- Modify: `Source/Plugin/PluginProcessor.cpp`
- Modify: `Tests/ProcessorTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `apvts`, `getLearningRows`, `getDiagnostics`, `getCurrentStep`, `setProgressionText`, `exportMidiFile`.
- Produces: `ChordTextAudioProcessorEditor`; `createEditor()` returns it.

- [ ] **Step 1: Write the failing editor smoke test**

```cpp
{
    ChordTextAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    CHECK (editor != nullptr);
    CHECK (editor->getWidth() == 760);
    CHECK (editor->getHeight() == 560);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: `createEditor()` returns `nullptr`, or the target fails because `PluginEditor.cpp` is not yet in the sources.

- [ ] **Step 3: Implement the editor**

Create a fixed 760×560 `ChordTextAudioProcessorEditor` with:

- A multi-line `juce::TextEditor` for the progression.
- `juce::ComboBox` controls for key, mode, voicing, and range, bound with `AudioProcessorValueTreeState::ComboBoxAttachment`.
- A `juce::Slider` for velocity, bound with `SliderAttachment`.
- A `juce::ToggleButton` for loop, bound with `ButtonAttachment`.
- A `juce::TextButton` for **Export .mid**, disabled while diagnostics are non-empty or there are no notes.
- A `juce::Label` status line, including “No transport available” when `isTransportAvailable()` is false.
- A `juce::TableListBox` with columns Chord, Degree, Notes, backed by a `TableListBoxModel` that reads `getLearningRows()`.

Text changes are debounced by 150 ms before calling `setProgressionText`. A 30 Hz timer refreshes diagnostics, rows, status, and the current-step highlight. The export button opens an async `juce::FileChooser` and calls `exportMidiFile` on the message thread. Keep the `FileChooser` alive as a member until its callback completes.

Add `Source/Plugin/PluginEditor.cpp` to `PLUGIN_SOURCES` and to `ProcessorTests` sources. `createEditor()` returns `new ChordTextAudioProcessorEditor (*this)`.

- [ ] **Step 4: Build and run the tests**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: `0 failures`.

- [ ] **Step 5: Verify the editor manually**

Build the VST3, load it in FL Studio, and confirm:

- Typing changes the learning table.
- Key, mode, voicing, range, velocity, and loop controls work.
- The current chord is highlighted during playback.
- Invalid text shows a diagnostic and keeps the last valid progression playing.
- Export writes a `.mid` file.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt Source/Plugin Tests/ProcessorTest.cpp
git commit -m "Add editor and learning table"
```

---

### Task 11: Documentation and final verification

**Files:**
- Modify: `README.md`
- Modify: `build.sh`
- Modify: `build.bat`
- Modify: `CMakeLists.txt` if test targets need updating

**Interfaces:**
- Consumes: all prior tasks.
- Produces: a documented, verified project.

- [ ] **Step 1: Finish the README**

Document:

- prerequisites and the pinned JUCE clone command,
- configure/build commands and both build scripts,
- VST3 output path and FL Studio routing/install instructions,
- the complete supported syntax with examples,
- how to run `TheoryTests` and `ProcessorTests`,
- the learning table and export behavior,
- licensing,
- known limitations and out-of-scope features,
- the manual FL Studio checklist from the spec.

- [ ] **Step 2: Run the full verification**

```bash
./build.sh --test
```

Expected: both test executables report `0 failures`, and the VST3 is present under `build/ChordText_artefacts/Release/VST3/`.

- [ ] **Step 3: Run the manual checklist**

In FL Studio 2025:

1. Route `Chord Text` to an instrument and hear the default progression.
2. Change text to `C, Cmaj7, Dmaj7, Emin9, Fmaj7` and confirm the notes.
3. Change to `i-iv-V` in A minor and confirm Am, Dm, E.
4. Test explicit durations.
5. Switch voice-led and root-position.
6. Start, stop, seek, loop, and bypass; confirm no stuck notes.
7. Record into the piano roll and verify timing.
8. Export `.mid`, import it, and compare with live playback.
9. Enter invalid text and confirm the last valid progression keeps playing.
10. Save and reload the project; confirm text and settings restore.

- [ ] **Step 4: Optional plugin validation**

If `pluginval` is available, run it on the built VST3 and fix any reported failures that affect this plugin.

- [ ] **Step 5: Commit**

```bash
git add README.md build.sh build.bat CMakeLists.txt
git commit -m "Document, test, and verify the chord generator"
```

---

## Verification Summary

| Spec requirement | Verified by |
|---|---|
| Absolute and Roman parsing | Tasks 4–5 theory tests |
| Default and explicit durations | Task 5 theory tests |
| Hyphen-separated progressions | Task 5 theory tests |
| Voice-led and root-position | Task 6 theory tests, Task 10 manual check |
| Live transport and loop | Tasks 7–8 processor tests |
| No stuck notes | Tasks 7 and 8 processor tests, Task 11 manual check |
| MIDI export | Task 9 processor tests, Task 11 import check |
| Parameters and state restore | Task 8 processor tests |
| Learning table and diagnostics | Task 10 editor, manual check |
| Build from clean checkout | Task 11 `./build.sh --test` |
| FL Studio MIDI routing | Task 1 manual gate, Task 11 manual check |
