# Chord Text — Command-Line Debug Tool Design

Date: 2026-10-23
Status: Draft for review

## 1. Intent

Chord Text currently requires a VST3 host to exercise its progression syntax.
That makes syntax experiments slow and couples syntax debugging to DAW setup.
This feature adds a standalone command-line utility that parses progression text
and reports what the plugin would do, without JUCE, audio, or a host.

The utility serves two workflows:

1. **One-shot checks** in scripts or a terminal:
   `chordtext-cli --key A --mode minor "i-iv-V"`.
2. **Interactive exploration** in a REPL where settings stay sticky while the
   user iterates on syntax.

It also writes a Standard MIDI File so the user can audition or inspect the
result outside the plugin.

### Success criteria

1. `chordtext-cli.exe` builds and runs without loading the VST3 or any host.
2. One-shot mode prints the parsed chords with symbol, functional degree,
   spelled tones, duration, and voiced MIDI pitches.
3. Settings default to the plugin's defaults and can be overridden with
   `--key`, `--mode`, `--voicing`, `--range`, and `--velocity`.
4. Parse errors are reported with the same wording as the plugin editor and
   produce a non-zero exit code.
5. `--json` emits a documented, correctly escaped JSON report.
6. Running with no progression and an interactive terminal starts a REPL with
   sticky settings and the commands listed below.
7. `--out` and `:export` write one loop as a MIDI file that is musically
   equivalent to the plugin's export: tempo, 4/4, track name, 960 PPQ,
   channel 1, note-offs before note-ons at equal ticks.
8. The plugin and the CLI share one MIDI-file writer and one voicing-note
   namer, so their output cannot drift.
9. Existing `TheoryTests` and `ProcessorTests` still pass, and a new
   `CliTests` suite covers option parsing, formatting, JSON, and REPL state.
10. The README documents usage, options, output, JSON, exit codes, and the
    build/test changes.

### Out of scope

- Audio playback or a built-in synth.
- Any graphical interface.
- REPL history, line editing, or tab completion.
- Host transport sync, playhead, or live MIDI.
- Reading plugin presets or host project state.
- Installer or distribution changes.
- Changing theory behaviour, chord syntax, voicing rules, or plugin UI.

## 2. Current state

- `Source/Theory/` is JUCE-free and owns parsing, spelling, keys, voicing, and
  the `RenderPlan`.
- `Source/Plugin/PluginProcessor.cpp` builds the plan from the same theory
  calls and owns MIDI export through `juce::MidiFile` in `writeMidiFile()`.
- `Tests/TheoryTest.cpp` links only the theory sources.
- `Tests/ProcessorTest.cpp` is a JUCE console app that reads exported MIDI back
  with `juce::MidiFile`, including tempo, time-signature, and drag-clip cases.
- `build.sh` / `build.bat` run TheoryTests and ProcessorTests.
- `Source/Plugin/PluginEditor.cpp` formats diagnostics as
  `Error at character <offset>: <message> (<token>)`, omitting the
  parenthetical when the token is empty.

## 3. Command surface

New executable: `build/Cli/chordtext-cli.exe`, CMake target `ChordTextCli`.

```text
chordtext-cli [options] [progression]
```

- Exactly one optional positional progression. A second positional is a usage
  error.
- If a progression is supplied, run one-shot mode; stdin is ignored.
- If no progression is supplied:
  - interactive stdin: start the REPL;
  - non-interactive stdin: read all of stdin as the progression text. An
    immediately closed or empty stdin is therefore an empty progression.
- `--out` requires a progression argument or piped stdin; using it with an
  interactive REPL start is a usage error. Use `:export` inside the REPL.
- `--` ends option parsing; the next argument is the progression even if it
  starts with `-`.
- Options accept both `--name value` and `--name=value`, except boolean flags;
  boolean flags reject an attached value.
- Option values are case-insensitive where noted. If an option is repeated,
  the last occurrence wins.
- `--help` prints usage to stdout and exits 0, regardless of other arguments.

### 3.1 Options

| Option | Values | Default |
|---|---|---|
| `--key <tonic>` | `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B`, case-insensitive | `C` |
| `--mode <major\|minor>` | case-insensitive | `major` |
| `--voicing <voice-led\|root-position>` | case-insensitive; `root` is an alias for `root-position` | `voice-led` |
| `--range <C2-C5\|C3-C6\|C4-C7>` | case-insensitive | `C3-C6` |
| `--velocity <1-127>` | integer | `100` |
| `--bpm <4-999>` | finite number in range | `120` |
| `--json` | flag | off |
| `--out <path.mid>` | file path | off |
| `--help` | flag | — |

Defaults are the plugin's defaults. `--bpm` only affects a written MIDI file;
it is reported in JSON but has no effect on the table.

Invalid values are usage errors. Missing option values are usage errors.
Unknown options are usage errors.

### 3.2 Exit codes

| Code | Meaning |
|---|---|
| 0 | Progression parsed successfully (including an empty progression) and any requested file was written |
| 1 | The progression has parse errors |
| 2 | Usage error, or a requested file could not be written |

In one-shot mode, results go to stdout. Usage errors always go to stderr.
I/O errors go to stderr in human mode and into the JSON `error` field in JSON
mode. In REPL mode, normal exit is always 0, including after earlier parse
errors.

### 3.3 REPL

Started when no progression argument is supplied and stdin is an interactive
terminal. The banner includes a current-settings line and a hint to use
`:help`. The banner and prompt go to stdout. The prompt is `chord> `.
Command names are lowercase as listed; their values follow the same
case-insensitive rules as the matching options.

- A non-empty line that does not start with `:` is parsed as a progression and
  reported with the current settings.
- A blank or whitespace-only line is ignored.
- A line starting with `:` is a command.
- Settings stay sticky until changed.
- Parse errors do not exit the REPL.

Commands:

| Command | Behaviour |
|---|---|
| `:key <tonic>` | Set the key tonic; same validation as `--key` |
| `:mode <major\|minor>` | Set the mode |
| `:voicing <voice-led\|root-position>` | Set the voicing mode |
| `:range <C2-C5\|C3-C6\|C4-C7>` | Set the range |
| `:velocity <1-127>` | Set the velocity |
| `:bpm <4-999>` | Set the MIDI tempo |
| `:json <on\|off>` | Switch report formatting |
| `:export <path.mid>` | Write the last successfully parsed progression with the current settings |
| `:help` | List commands and current settings |
| `:quit`, `:q` | Exit |

- After a successful setting change, the command prints the current-settings
  line.
- After an invalid command or value, the command prints a short error and keeps
  the previous value.
- `:export` with no last valid progression prints an error and writes nothing.
- `:export` re-parses the last valid text with the current settings, so the
  written file always matches what a report would show now.
- `:export` takes the remainder of the line as the path, trimmed, so paths may
  contain spaces.
- EOF (Ctrl+Z on Windows) exits the REPL.
- Command feedback goes to stderr; progression reports go to stdout. This
  keeps stdout parseable when `:json on` is active.

## 4. Report model and formats

A report is built from the same pipeline as the plugin:

```text
parseProgression -> voiceProgression -> buildRenderPlan
```

`CliSettings` stores `PitchClass tonic`, `Mode mode`, `VoicingMode voicing`,
`int rangeIndex` (0 = C2-C5, 1 = C3-C6, 2 = C4-C7), `int velocity`,
`double bpm`, and `bool json`. The range index maps to the plugin's
`{36,72}`, `{48,84}`, `{60,96}` MIDI bounds.

The report contains:

- the input text, verbatim as supplied (JSON) or whitespace-collapsed for the
  human header,
- the effective settings,
- validity,
- diagnostics,
- one row per chord with source token, normalized symbol, degree label, chord
  tones, voiced spelled notes, voiced MIDI pitches, start beat, and duration,
- the render plan used for MIDI output (empty when invalid),
- an optional written MIDI path,
- an optional file error.

### 4.1 Human-readable output

```text
Progression: Cmaj7, Am7, Dm7, G7
Key: C major · Voicing: root-position · Range: C2-C5 · Velocity: 100

Chord  Degree  Notes    Beats  MIDI
Cmaj7  Imaj7   C E G B  4      36 40 43 47
Am7    vi7     A C E G  4      45 48 52 55
Dm7    ii7     D F A C  4      38 41 45 48
G7     V7      G B D F  4      43 47 50 53
```

Rules:

- The `Progression:` header collapses runs of whitespace and newlines to one
  space and trims the result. Empty text shows `(empty)`.
- The settings line shows key tonic and mode, voicing, range name, and
  velocity. BPM is omitted because it does not affect the table.
- Columns are left-aligned with two spaces between them. Widths are computed
  from the content.
- `Notes` is the voiced chord spelling, in the same order as `MIDI`, using each
  chord tone's spelling. It mirrors the plugin's learning table, so an
  inversion appears in voicing order, and a slash bass may repeat a pitch class.
- `MIDI` is the same voicing as space-separated MIDI note numbers after
  voicing mode, range, and slash bass.
- `Beats` uses compact formatting: the shortest decimal representation that
  round-trips to the same double; integral values omit the decimal point (for
  example `4`, `1.5`).
- Numeric formatting is locale-independent.
- After a successful `--out` or `:export`, a final line reports the absolute
  path written. In one-shot mode it goes to stdout; in the REPL it is command
  feedback and goes to stderr.

Diagnostics replace the table:

```text
Progression: C, H, C99
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100

2 errors:
Error at character 3: unknown chord root (H)
Error at character 6: unknown extension "99" (C99)
```

- The count line is `1 error:` or `N errors:`.
- Each line uses the editor's wording exactly; the parenthetical token is
  omitted when the token is empty.
- Diagnostics are shown and the exit code is 1 when called one-shot.

An empty progression is valid and prints `Empty progression (no chords).`
The CLI reports only the current input; it never shows a stale table from an
earlier valid progression.

### 4.2 JSON output

`--json` prints one pretty-printed object with two-space indentation and a
trailing newline:

```json
{
  "text": "Cmaj7, Am7",
  "settings": {
    "key": "C",
    "mode": "major",
    "voicing": "voice-led",
    "range": "C3-C6",
    "velocity": 100,
    "bpm": 120
  },
  "valid": true,
  "diagnostics": [],
  "chords": [
    {
      "source": "Cmaj7",
      "symbol": "Cmaj7",
      "degree": "Imaj7",
      "spelledTones": ["C", "E", "G", "B"],
      "notes": ["C", "E", "G", "B"],
      "voicing": [48, 52, 55, 59],
      "startBeat": 0,
      "durationBeats": 4
    }
  ],
  "midiFile": null,
  "error": null
}
```

Rules:

- `valid` is false when parse diagnostics are present. Then `chords` is empty
  and `diagnostics` is populated.
- An empty but valid progression has `"valid": true` and `"chords": []`.
- Setting strings are canonical: key spelling as written in the option list,
  `mode` as `major` or `minor`, `voicing` as `voice-led` or `root-position`,
  and `range` as `C2-C5`, `C3-C6` or `C4-C7`.
- `spelledTones` is the chord's tones in quality order, independent of
  voicing.
- `notes` is the voiced spelling, one entry per `voicing` value, matching the
  human table's `Notes` column.
- `startBeat` is cumulative across chords.
- `midiFile` is an absolute path string after a successful write, otherwise
  null.
- `error` is a string for a file I/O or empty-export failure, otherwise null.
- Only one of parse diagnostics and `error` is meaningfully populated.
- Strings are escaped per JSON: `"`, `\`, `\b`, `\f`, `\n`, `\r`, `\t`, other
  control characters as `\u00XX`. Non-ASCII UTF-8 is preserved.
- Numbers are locale-independent and use the shortest representation that
  round-trips; integral numbers have no decimal point.
- Whitespace and indentation are not part of the contract; consumers must
  parse the document.
- In JSON mode, success and failure information lives in the document; no
  human result text is written to stdout.
- Usage errors remain human-readable on stderr even when `--json` is present.
- In the REPL, each report is one JSON document on stdout; command feedback is
  on stderr.

## 5. Architecture and components

### 5.1 Shared MIDI writer

New JUCE-free files `Source/Theory/MidiFileWriter.h/.cpp`, added to
`THEORY_SOURCES`:

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

Serialisation rules, matching the current plugin export:

- Standard MIDI File type 0, one track.
- 960 ticks per quarter note.
- Track name meta event `Chord Text` at tick 0.
- When `includeTempoAndTimeSignature` is true, add a tempo meta event at tick
  0 using `lround(60000000 / bpm)` microseconds per quarter (clamped to the
  24-bit range) and a 4/4 time-signature event.
- Note events use status `0x90` or `0x80` combined with `channel - 1` in the
  low nibble (1-based event channel, 0-based status nibble).
- Note-ons carry the event velocity; note-offs carry velocity 0.
- Events are ordered by beat ascending, note-offs before note-ons at the same
  beat, then pitch ascending.
- End-of-track event after the last event.
- Byte-for-byte equality with JUCE output is not required; musical content and
  ordering are.
- `writeMidiFile` truncates and writes the file, returning false with
  `Cannot open <path> for writing.` or `Cannot write the MIDI file <path>.`
  It does not delete a partial file; the caller decides.

The plugin's drag-clip path keeps its existing delete-on-failure behaviour.

### 5.2 Shared note naming

The plugin's learning rows currently use `noteNameForPitch` and
`notesForVoicing`, which live in an anonymous namespace in
`PluginProcessor.cpp`. Move them to the JUCE-free theory layer, declared in
`Source/Theory/Chord.h` and implemented in `Source/Theory/Chord.cpp`:

```cpp
std::string noteNameForPitch (const Chord& chord, int midi);
std::string notesForVoicing (const Chord& chord, const std::vector<int>& voicing);
```

- `noteNameForPitch` returns the chord tone whose pitch class matches `midi`,
  preserving the chord's spelling. If no tone matches, it falls back to a
  sharp-based pitch-class name without an octave.
- `notesForVoicing` returns the space-separated names for a voicing, in
  voicing order, and may repeat a name for a slash-bass duplicate.
- `PluginProcessor` deletes its local copies and calls the theory functions.
  `LearningRow.notes` behaviour is unchanged.
- The CLI uses the same functions for its `Notes` column and JSON `notes`.

### 5.3 Plugin change

`PluginProcessor::writeMidiFile` keeps its diagnostics/empty guards and error
messages, then delegates to `chordtext::writeMidiFile`:

- path: `std::filesystem::u8path(file.getFullPathName().toStdString())`
  (absolute, UTF-8);
- bpm: the cached `lastHostBpm` when valid, else 120;
- `includeTempoAndTimeSignature = (mode == MidiFileMode::exportFile)`;
- track name `Chord Text`, 960 PPQ.

The `juce::MidiFile` dependency is removed from this path. No other processor
behaviour changes.

### 5.4 CLI code

New `Source/Cli/`:

| File | Responsibility |
|---|---|
| `CliSettings.h/.cpp` | Settings struct, defaults, canonical names, validation of key/mode/voicing/range/velocity/bpm |
| `CliOptions.h/.cpp` | argv parser, help text, positional handling, exit-code classification |
| `CliReport.h/.cpp` | Parse/voice/plan pipeline, report struct, human and JSON formatting, diagnostic wording |
| `CliSession.h/.cpp` | REPL state machine, sticky settings, command handling, last-valid text, export |
| `main.cpp` | TTY/stdin detection, Windows console UTF-8 setup, argument conversion, one-shot/REPL dispatch, file output |

The CLI core (`CliSettings`, `CliOptions`, `CliReport`, `CliSession`) has no
JUCE dependency and no global state, so it can be unit-tested directly.

`main.cpp` is excluded from the test target because it defines `main`.

`CliSession` exposes a testable line handler that keeps report output and
command feedback separate:

```cpp
struct CliSessionOutput
{
    std::string report;   // stdout
    std::string feedback; // stderr
    bool quit = false;
};

CliSessionOutput handleLine (std::string_view line);
```

This keeps stdout JSON-clean when `:json on` is active.

### 5.5 Windows text handling

- Under `_WIN32`, use `wmain` and convert arguments to UTF-8 with
  `WideCharToMultiByte(CP_UTF8, ...)`, so chord tokens containing `°` or `ø`
  survive the command line.
- Call `SetConsoleCP(CP_UTF8)` and `SetConsoleOutputCP(CP_UTF8)` in `main`.
- Detect interactive stdin with `_isatty(_fileno(stdin))` under `_WIN32`.
- Use `std::filesystem::u8path` for user-supplied paths.
- All formatting and parsing uses the classic locale.

## 6. Error handling and edge cases

- Parse errors: print diagnostics, no table, no MIDI write, exit 1.
- Empty progression: valid; no table; `--out` / `:export` refuses with
  `Cannot write MIDI for an empty progression.` and exit code 2 in one-shot
  mode.
- Bad option value, unknown option, missing value, or extra positional:
  short usage message on stderr, exit 2. `--help` still works.
- If the path does not already end in `.mid` (case-insensitive), its final
  extension is replaced with `.mid`; a path with no extension gains one. This
  matches the plugin's `withFileExtension(".mid")` behaviour.
- Relative output paths resolve against the current working directory; the
  success message and JSON report use the absolute path.
- If stdin cannot be read in piped-input mode, the tool reports an I/O error
  and exits 2.
- If the MIDI file cannot be opened or written, no success line is printed;
  the error goes to stderr (or `"error"` in JSON) and the exit code is 2. The
  CLI deletes any partial output file left by the failed write.
- REPL setting errors do not change the setting and do not exit.
- REPL `:export` with no valid text prints an error and writes nothing.
- A REPL progression line that fails parsing does not overwrite the last valid
  text.
- Input with only separators is an empty progression.
- Very long progressions are bounded only by available memory; no artificial
  limit is introduced.

## 7. Build, tests, and documentation

### 7.1 CMake

- Add `Source/Theory/MidiFileWriter.cpp` to `THEORY_SOURCES`.
- Add CLI core sources (excluding `main.cpp`) to a list used by both targets.
- `ChordTextCli`: CLI core + `Source/Cli/main.cpp` + `THEORY_SOURCES`; no JUCE
  libraries; output directory `${CMAKE_BINARY_DIR}/Cli`; executable name
  `chordtext-cli`.
- `CliTests`: `Tests/CliTest.cpp` + CLI core + `THEORY_SOURCES`; output
  directory `${CMAKE_BINARY_DIR}/Tests`; executable `CliTests.exe`.
- The existing `/utf-8` MSVC option already covers the new sources.
- The new targets must build without warnings.

### 7.2 Tests

`TheoryTests` additions:

- `noteNameForPitch` preserves chord spelling and falls back to a sharp-based
  pitch-class name for unmatched pitches;
- `notesForVoicing` returns voicing order and repeats a pitch class for slash
  bass, matching the plugin's historical output;
- serialise a fixed plan and assert `MThd`, header length 6, format 0, one
  track, division 960;
- deterministic output for identical input;
- 120 BPM encodes a 500000-microsecond tempo event;
- drag settings omit tempo and time-signature events;
- end-of-track meta event terminates the track;
- a small test-only SMF reader validates note order, pitches, velocities,
  channel, durations, and note-off-before-note-on at equal ticks.

`ProcessorTests`:

- no expectation changes;
- existing export and drag read-back checks must pass, proving plugin parity
  with the shared writer.

New `CliTests`:

- defaults match the plugin;
- every option with both `--name value` and `--name=value`;
- case-insensitive values, `root` alias, `--` terminator, `--help`;
- unknown option, missing value, extra positional, invalid key/mode/voicing/
  range/velocity/bpm;
- exit-code classification for success, parse errors, usage errors, and write
  failure;
- human table content, column presence, beat formatting, empty progression,
  whitespace/newline collapsing, and exact diagnostic wording;
- `Notes` matches `notesForVoicing` for an inversion and a slash bass;
- JSON `spelledTones`, `notes`, and `voicing` are consistent for the same
  chord;
- JSON shape for valid, invalid, and empty input, cumulative `startBeat`,
  escaping of quotes/backslashes/newlines, and locale-independent numbers;
- `:export` before any valid input, after valid input, and after a later
  invalid line, using a temporary directory for written files;
- sticky settings, `:json on|off`, unknown commands, and `:quit`.

### 7.3 Build scripts

`build.sh --test` and `build.bat test` run `TheoryTests`, `ProcessorTests`,
then `CliTests`, stopping at the first failure.

### 7.4 Documentation

README gains a `Command-line utility` section covering:

- one-shot, stdin, and REPL invocation;
- the option table and defaults;
- human output with an example;
- the JSON schema with an example;
- exit codes;
- REPL commands;
- `:export` semantics;
- the Windows-only console and UTF-8 notes.

The repository layout and Tests sections are updated for `Source/Cli/`,
`MidiFileWriter`, and `CliTests`. Known limitations gain the items below.

## 8. Known limitations for this feature

- Windows x64 only, consistent with the project.
- No audio preview; the tool reports text and writes MIDI files only.
- The REPL is line-oriented with no history, editing, or completion.
- `--bpm` defaults to 120 because there is no host tempo to observe.
- JSON in the REPL is one document per report interleaved with human command
  feedback on stderr, so it is not a single JSON stream.
- The shared writer replaces the JUCE export implementation; parity is guarded
  by the existing read-back tests rather than byte-for-byte comparison.
