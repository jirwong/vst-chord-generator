# Chord Text

Chord Text is a Windows x64 VST3 MIDI generator. Type a chord progression as chord
symbols or Roman numerals, and the plugin plays it in sync with the host transport
or exports it as a Standard MIDI File. You can also drag one loop from the editor
into a DAW such as Bitwig as a MIDI clip. The editor shows a learning table with
each chord's functional degree, spelled chord tones, and duration in beats.

The plugin produces MIDI only. It has no audio output and no built-in synth, so it
must be routed to an instrument in the host.

For the full syntax reference, see [SYNTAX.md](SYNTAX.md).

## Prerequisites

- Windows 10 or 11, 64-bit.
- Visual Studio 2022 Build Tools with the "Desktop development with C++" workload
  (MSVC 14.44 or compatible).
- CMake 3.22 or newer. The copy shipped with Build Tools is sufficient; the build
  scripts find it by full path when it is not on `PATH`.
- Git.
- A VST3-compatible DAW for manual in-host verification (optional for building and
  running the automated tests).

The project uses C++17 and depends only on JUCE and the C++ standard library. The
theory library itself has no JUCE dependency.

## Get JUCE 8.0.15

JUCE is not vendored in this repository. Clone the pinned version into the ignored
`libs/JUCE` directory:

```bash
git clone --depth 1 --branch 8.0.15 --recurse-submodules --shallow-submodules https://github.com/juce-framework/JUCE.git libs/JUCE
```

Confirm the checkout if needed:

```bash
git -C libs/JUCE describe --tags --always
```

## Configure and build

Use the Visual Studio 2022 x64 generator. Do not use Ninja.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

If `cmake` is not on `PATH`, use the Build Tools copy explicitly, for example:

```bash
"/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" -S . -B build -G "Visual Studio 17 2022" -A x64
"/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --parallel
```

### Build scripts

The repository provides equivalent Bash and Command Prompt wrappers. Both configure
the project on first use, build the Release configuration, and optionally run the
test suites.

Bash (Git Bash, MSYS2, or WSL with a Windows CMake):

```bash
./build.sh          # configure if needed, then build Release
./build.sh --test   # build, then run TheoryTests, ProcessorTests, and CliTests
```

Command Prompt:

```bat
build.bat
build.bat test
```

Both scripts return a non-zero exit code if configuration, the build, or any test
suite fails.

### Build outputs

- VST3 bundle: `build/ChordText_artefacts/Release/VST3/Chord Text.vst3`
- Binary inside the bundle:
  `build/ChordText_artefacts/Release/VST3/Chord Text.vst3/Contents/x86_64-win/Chord Text.vst3`
- Command-line utility: `build/Cli/chordtext-cli.exe`
- Test executables: `build/Tests/TheoryTests.exe`,
  `build/Tests/ProcessorTests.exe`, and `build/Tests/CliTests.exe`

## Install and route in a DAW

Chord Text is a VST3 MIDI effect (`IS_MIDI_EFFECT TRUE`, MIDI input and output
enabled, no audio buses). Incoming MIDI is ignored in v1.

### Install

Either copy the whole `build/ChordText_artefacts/Release/VST3/Chord Text.vst3`
bundle into the system VST3 folder:

```text
C:\Program Files\Common Files\VST3
```

or leave it in the build tree and add the folder as a plugin search path:

1. In the DAW's plugin settings, add
   `...\chord-generator\build\ChordText_artefacts\Release\VST3`
   to the additional VST3 search paths.
2. Start a plugin scan and confirm that **Chord Text** appears as a MIDI effect /
   generator.

### Route to an instrument

The plugin must drive another instrument; it makes no sound by itself. Two common
routes are:

- **Host plugin chain:** insert **Chord Text** before an instrument (for example a
  synth or sampler) in the same channel or instrument chain so the plugin's MIDI
  output feeds the instrument's MIDI input directly.
- **MIDI output ports:** load Chord Text in one channel, then route its MIDI output
  port to a second channel/instrument whose MIDI input port matches.

The exact routing steps depend on the DAW and its plugin wrapper. In-host routing is
part of the manual checklist below and has **not** been verified in this environment.

## Quick start

1. Load Chord Text in a channel and route it to an instrument.
2. Press Play in the DAW. The default progression
   `Cmaj7, Am7, Dm7, G7` plays, one chord per 4/4 bar, and loops.
3. Type a progression in the text box. Parsing is debounced by 150 ms, so playback
   updates shortly after typing stops. Press **Regenerate** to apply the current
   text immediately.
4. Use the key, mode, voicing, range, velocity, and loop controls to shape the
   output.
5. Read the learning table for each chord's degree, spelled notes, and duration,
   then drag the clip preview into the DAW or export a `.mid` file if desired.

Live generation requires host playhead data with a PPQ position and tempo. The
status line reports `Transport ready` while the transport plays, `Transport
stopped` when a playhead position exists but the transport is not playing, and
`No transport available` when the host supplies no usable position. In the last
case no notes are generated; the plugin does not free-run.

The progression is anchored to song position 0. Starting or seeking into the middle
of a chord plays that chord from the start of the block, and the progression loops
with a length equal to the sum of the chord durations. Loop is on by default; with
loop off, the progression plays once. Stop, seek, bypass, plan changes, and loop wrap
release active notes before new ones are emitted. When the host calls
`prepareToPlay` again, pitches that were sounding are queued for release and their
note-offs are emitted at sample 0 of the next block before any new events.
`releaseResources` cannot emit MIDI, so it only clears internal note tracking; the
host is expected to flush sounding notes when it releases the plugin.

## Regenerate and automatic parsing

**Regenerate** sits on the progression header row. Clicking it cancels any pending
debounce, parses the text in the editor immediately, and rebuilds the playback plan
and learning table from the current text and settings. It forces a rebuild even when
the text is unchanged.

Automatic parsing remains enabled: the text is still parsed 150 ms after the last
edit. Regenerate is an explicit alternative to waiting for that timer, not a
replacement for it.

If the text is invalid, Regenerate does not replace the last valid plan or table
rows. The existing progression keeps playing and the diagnostics area shows the
errors. An empty progression is valid; Regenerate then produces an empty table and
disables export and dragging.

## Syntax reference

The full, canonical syntax guide is [SYNTAX.md](SYNTAX.md). It covers
progressions and separators, durations, absolute chords and aliases,
alterations, slash bass, Roman numerals, keys, grammar, diagnostics, and common
mistakes.

Quick examples:

```text
Cmaj7, Am7, Dm7, G7
i-iv-V
C:8, Am7:4, F:4, G:4
C/E, F#m7b5/C
```

Every chord lasts 4 beats by default, and separators can be mixed. Absolute
chords and Roman numerals can be used in the same progression. The plugin
editor and `chordtext-cli` share the parser, so the same text produces the same
result in both. To check syntax without a DAW, run `chordtext-cli "C, H"`.

The key selector matters for Roman numerals and the Degree column; it does not
transpose absolute chords.

### The learning table

The table shows the last valid progression:

| Chord | Degree | Notes | Beats |
|---|---|---|---|
| Cmaj7 | Imaj7 | C E G B | 4 |
| Am7 | vi7 | A C E G | 4 |
| Dm7 | ii7 | D F A C | 4 |
| G7 | V7 | G B D F | 4 |

The Beats column is the chord's effective duration, including the default of 4
beats when the input has no `:N` suffix. It is displayed as a compact number, so a
chord written as `Am7:1.5` shows `1.5`, not `1.5 beats`. The Notes column follows
the current voicing and range and uses each chord's spelled tones, so Roman
numerals inherit the key's spelling. The Degree column is a normalized functional
label; absolute chords are labeled best-effort relative to the selected key using
the root letter and accidental (not by nearest pitch class). Chromatic or ambiguous
roots use a best-effort `b`/`#` label. The row for the currently playing chord is
highlighted during playback.

The table continues to show the last valid progression while the text is invalid.

### Errors and diagnostics

Every parse error identifies the failing token, a zero-based character offset
into the progression text, and a short message; the editor lists them below the
learning table. If any token is invalid, no new plan is published: the last
valid progression keeps playing, the table continues to show it, and export is
disabled. See [SYNTAX.md](SYNTAX.md#6-errors-and-diagnostics) for the exact
format and examples.

## Controls and saved state

| Control | Default | Notes |
|---|---|---|
| Progression text | `Cmaj7, Am7, Dm7, G7` | Multi-line, parsed 150 ms after the last edit |
| Regenerate | — | Parses the current text and rebuilds immediately |
| Key | C | 17 tonic spellings |
| Mode | Major | Major or minor |
| Voicing | Voice-led | Voice-led or Root position |
| Range | C3–C6 | C2–C5, C3–C6, or C4–C7 |
| Velocity | 100 | 1–127 |
| Loop | On | On or off |
| Drag to DAW | — | Drags one loop as a `.mid` file; enabled for valid, non-empty progressions |
| Reveal last clip | — | Opens Explorer at the last drag file; enabled after a drag in this session |

All notes are sent on MIDI channel 1. Root-position voicing places the root at the
lowest octave that fits the range and stacks the remaining tones above it. Voice-led
voicing keeps the first chord in root position and then chooses the inversion and
octave placement that minimizes movement from the previous chord, staying within the
selected range. Slash bass remains the lowest note in both modes. The upper voices
contain each chord tone once. A slash bass may duplicate its pitch class an octave
below the upper chord tones (for example, `C/E` sounds two E's); v1 has no other
octave doubling.

The project state saves the progression text, key, mode, voicing, range, velocity,
and loop setting through stable parameter IDs (`key`, `mode`, `voicing`, `range`,
`velocity`, `loop`) plus the `progressionText` property. Restoring a project rebuilds
the plan but does not start playback. Hosts are expected to restore state on the
message thread; if state is restored on another thread, the plan rebuild is deferred
to the message queue.

## MIDI export

Click **Export .mid** to write one complete loop of the current progression. Export
is separate from dragging; unlike a drag file, an exported file always includes the
tempo and 4/4 time-signature metadata. The writer uses the same render plan as live
playback:

- the last host tempo observed during processing, or 120 BPM before any tempo has
  been observed;
- 4/4 time signature;
- track name `Chord Text`;
- 960 ticks per quarter note;
- deterministic event order with note-offs before note-ons at the same tick;
- channel 1 note events.

`AudioPlayHead::getPosition()` is only valid inside `processBlock`, so the processor
caches the last valid host tempo in an atomic and export never queries the playhead.
Export therefore still works with no playhead, at 120 BPM until the host has
processed a block.

The button is disabled when the text has errors or the progression is empty, and
while a file chooser is already open. Export runs on the message thread and appends
`.mid` if the chosen filename lacks it. The status line reports the file name or the
failure reason.

Export retains the tempo event (the last observed host tempo, or 120 BPM) and the
4/4 time signature; only the drag path omits them.

## Dragging clips into a DAW

The editor contains a clip-shaped **Drag to DAW** preview. It shows the loop length,
for example `16 beats · 4 bars (4/4)`, computed as the sum of the chord durations.
A left-button drag starts only after a small mouse movement, so a click does not
create a file.

The preview is enabled after the last successful parse, when that plan contains at
least one note. While a debounced edit is pending it reflects the last parsed text,
not the characters just typed. An empty progression also disables the preview.
While it is disabled it shows `Fix errors to drag`, or
`Enter a progression to drag` when the progression is empty. Starting a drag forces
a parse of the current text first, using the same path as Regenerate. If parsing
fails, no file is created and the diagnostics are shown.

A drag writes exactly one loop cycle at the current key, mode, voicing, range, and
velocity settings. The file uses the same notes, 960 ticks per quarter note, MIDI
channel 1, track name `Chord Text`, and note-off-before-note-on ordering as live
playback and export. It deliberately contains **no tempo meta event and no
time-signature meta event**, so the DAW's project tempo and meter govern the clip.
If the host instead assumes 120 BPM, that is a host-behaviour risk noted below.

### File location and lifetime

Drag files are written to:

```text
Documents\ChordText\Clips
```

The folder is created if missing. Each file is named
`ChordText-YYYYMMDD-HHMMSS.mid` using local time, so repeated drags do not
overwrite earlier clips. If two drags happen in the same second, a numeric suffix is
added to the timestamped name instead of overwriting the existing file. Files are
never deleted automatically in v1: a clip may already be referenced by a saved DAW
project, and removing its source would break that project.

### Fallback: Reveal last clip

Every started drag leaves a real file in the clips folder, whether or not the DAW
accepts the drop. The plugin cannot detect whether the drop succeeded, so the status
line reports the saved filename and that the drag was requested, for example
`Clip saved: ChordText-20261022-143015.mid; drag requested.` If the drag could not be
queued, it reports
`Clip saved: ChordText-20261022-143015.mid; drag could not be queued.` Neither
message claims that the DAW accepted the drop.

**Reveal last clip** sits next to **Export .mid**. It is enabled once a drag file has
been created in the current plugin session and opens Explorer with that file
selected. The last drag path is session-only and is not saved with the project, so
Reveal starts disabled after reloading even if earlier clip files still exist in the
folder. If Bitwig ignores the external drag, click Reveal last clip and drag the file
from Explorer into Bitwig.

### Bitwig notes

Bitwig receives the plugin as a VST3 MIDI generator; load the plugin, then route
its MIDI output to an instrument as usual. The drag writes a file and starts an
operating-system file drag. It does not create a Bitwig clip through a native API,
because a VST3 plugin has no such API and Bitwig does not expose one for this
workflow. The resulting clip should follow the Bitwig project tempo because the file
has no tempo event. This behaviour is host-dependent and has not been verified here;
use the checklist below.

## Command-line utility

The build also produces a standalone console program,
`build/Cli/chordtext-cli.exe`, that parses a progression and reports what the plugin
would do — without a DAW, a VST3 host, or JUCE. It supports one-shot checks, piped
stdin, and an interactive REPL, and it can write a Standard MIDI File through the
same shared writer as the plugin.

```text
chordtext-cli [options] [progression]
```

There is at most one positional argument, the progression text. With a progression
argument the utility runs once and exits; stdin is ignored. With no argument and a
non-interactive (piped) stdin it reads all of stdin as the progression. With no
argument and an interactive terminal it starts the REPL. `--` ends option parsing,
so a progression beginning with `-` is taken literally.

One-shot:

```bash
./build/Cli/chordtext-cli.exe --key A --mode minor "i-iv-V"
```

```text
Progression: i-iv-V
Key: A minor · Voicing: voice-led · Range: C3-C6 · Velocity: 100

Chord  Degree  Notes   Beats  MIDI
Am     i       A C E   4      57 60 64
Dm     iv      A D F   4      57 62 65
E      V       G# B E  4      56 59 64
```

Piped stdin:

```bash
echo "C, H, C99" | ./build/Cli/chordtext-cli.exe
```

```text
Progression: C, H, C99
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100

2 errors:
Error at character 3: unknown chord root (H)
Error at character 6: unknown extension "9" (C99)
```

(exit code 1)

REPL (interactive terminal only): run `chordtext-cli` with no arguments. The banner
prints the current settings and the prompt is `chord> `; a session looks like:

```text
Chord Text CLI — :help for commands
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100

chord> Cmaj7
Progression: Cmaj7
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100

Chord  Degree  Notes    Beats  MIDI
Cmaj7  Imaj7   C E G B  4      48 52 55 59
chord> :key A
Key: A major · Voicing: voice-led · Range: C3-C6 · Velocity: 100
chord> :quit
```

The full command list is under [REPL](#repl).

### Options

| Option | Values | Default |
|---|---|---|
| `--key <tonic>` | `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B`, case-insensitive | `C` |
| `--mode <major\|minor>` | case-insensitive | `major` |
| `--voicing <voice-led\|root-position>` | case-insensitive; `root` is an alias for `root-position` | `voice-led` |
| `--range <C2-C5\|C3-C6\|C4-C7>` | case-insensitive | `C3-C6` |
| `--velocity <1-127>` | integer | `100` |
| `--bpm <4-999>` | finite number in range | `120` |
| `--json` | flag | off |
| `--out <path>` | file path | off |
| `--help` | flag | — |

Defaults are the plugin's defaults. `--bpm` only affects a written MIDI file; it is
reported in JSON but has no effect on the table. Options accept both `--name value`
and `--name=value`, except boolean flags, which reject an attached value. If an
option is repeated, the last occurrence wins. Unknown options, missing values, and
invalid values are usage errors.

### Human output

A successful report prints a `Progression:` header (with runs of whitespace and
newlines collapsed to single spaces, or `(empty)` for empty text), a settings line,
and a table:

```text
Progression: Cmaj7, Am7, Dm7, G7
Key: C major · Voicing: root-position · Range: C2-C5 · Velocity: 100

Chord  Degree  Notes    Beats  MIDI
Cmaj7  Imaj7   C E G B  4      36 40 43 47
Am7    vi7     A C E G  4      45 48 52 55
Dm7    ii7     D F A C  4      38 41 45 48
G7     V7      G B D F  4      43 47 50 53
```

Columns are left-aligned with two spaces between them. The `Notes` column follows
the current voicing, matching the plugin's learning table, so an inversion appears
in voicing order and a slash bass may repeat a pitch class. The `MIDI` column is the
same voicing as space-separated MIDI note numbers after voicing, range, and slash
bass are applied. `Beats` uses compact formatting: integral values omit the decimal
point (for example `4`, `1.5`). An empty progression is valid and prints
`Empty progression (no chords).`.

Diagnostics replace the table and use the editor's exact wording, one error per line
with the token's character offset:

```text
Progression: C, H, C99
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100

2 errors:
Error at character 3: unknown chord root (H)
Error at character 6: unknown extension "9" (C99)
```

The count line is `1 error:` or `N errors:`. The parenthetical token is omitted when
the token is empty. The CLI reports only the current input; it never shows a stale
table from an earlier valid progression.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Progression parsed successfully (including an empty progression) and any requested file was written |
| 1 | The progression has parse errors |
| 2 | Usage error, or a requested file could not be written |

In one-shot mode results go to stdout and usage errors to stderr. I/O errors go to
stderr in human mode and into the JSON `error` field in JSON mode. A REPL session
always exits 0, even after earlier parse errors.

### REPL

Running `chordtext-cli` with no arguments in an interactive terminal starts a REPL.
The banner prints a current-settings line and a hint to use `:help`, and the prompt
is `chord> `. Settings stay sticky across lines until changed. A non-empty line that
does not start with `:` is parsed as a progression and reported with the current
settings; a blank line is ignored; a parse error does not exit the REPL.

| Command | Behaviour |
|---|---|
| `:key <tonic>` | Set the key tonic; same validation as `--key` |
| `:mode <major\|minor>` | Set the mode |
| `:voicing <voice-led\|root-position>` | Set the voicing mode |
| `:range <C2-C5\|C3-C6\|C4-C7>` | Set the pitch range |
| `:velocity <1-127>` | Set the velocity |
| `:bpm <4-999>` | Set the MIDI tempo |
| `:json <on\|off>` | Switch report formatting between human and JSON |
| `:export <path>` | Write the last successfully parsed progression with the current settings |
| `:help` | List commands and current settings |
| `:quit`, `:q` | Exit |

After a successful setting change the command prints the current-settings line; an
invalid command or value prints a short error and keeps the previous value. `:export`
with no valid progression yet prints an error and writes nothing. EOF (Ctrl+Z on
Windows) exits the REPL. Command feedback goes to stderr and progression reports go
to stdout, so stdout stays parseable when `:json on` is active.

### JSON output

`--json` prints one pretty-printed object with two-space indentation and a trailing
newline:

```json
{
  "text": "Cmaj7",
  "settings": {
    "key": "C",
    "mode": "major",
    "voicing": "root-position",
    "range": "C2-C5",
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
      "voicing": [36, 40, 43, 47],
      "startBeat": 0,
      "durationBeats": 4
    }
  ],
  "midiFile": null,
  "error": null
}
```

This example was generated with `--voicing root-position --range C2-C5`; the default
settings produce `voice-led` and `C3-C6` (and the corresponding `voicing` numbers)
instead.

Field meanings:

- `settings` holds the effective settings as canonical strings (`key` as written in
  the option list, `mode` as `major`/`minor`, `voicing` as `voice-led`/
  `root-position`, `range` as `C2-C5`/`C3-C6`/`C4-C7`).
- `valid` is false when parse diagnostics are present; then `chords` is empty and
  `diagnostics` is populated with `offset`, `token`, and `message` entries. An empty
  but valid progression has `"valid": true` and `"chords": []`.
- `spelledTones` is the chord's tones in quality order, independent of voicing.
- `notes` is the voiced spelling, one entry per `voicing` value, matching the human
  table's `Notes` column.
- `voicing` is an array of integers identical in value to the human table's
  space-separated `MIDI` column.
- `startBeat` is cumulative across chords.
- `midiFile` is an absolute path string after a successful write, otherwise `null`.
- `error` is a string for a file I/O or empty-export failure, otherwise `null`.

Strings are escaped per JSON and numbers are locale-independent. Whitespace and
indentation are not part of the contract; consumers must parse the document. In JSON
mode no human result text is written to stdout; usage errors remain human-readable
on stderr.

### Writing MIDI files

`--out <path>` (one-shot or piped mode) and `:export <path>` (REPL) write one loop
of the current progression as a Standard MIDI File, using the same shared writer as
the plugin's export: 4/4 time signature, tempo from `--bpm` (default 120), track
name `Chord Text`, 960 ticks per quarter note, channel 1, and note-offs before
note-ons at equal ticks. `--out` requires a progression argument or piped input;
using it to start an interactive REPL is a usage error — use `:export` there.

If the path does not already end in `.mid` (case-insensitive), its final extension is
replaced with `.mid`; a path with no extension gains one. Relative paths resolve
against the current working directory, and the success line (and JSON `midiFile`)
report the absolute path. `:export` re-parses the last valid text with the current
settings, so the written file always matches what a report would show now. Writing to
an empty progression fails with exit code 2 in one-shot mode; the error goes to
stderr (or the JSON `error` field) and no `Wrote` line is printed.

### Windows and UTF-8

The utility is Windows x64 only, consistent with the project. It builds with `wmain`
and converts command-line arguments to UTF-8, so chord tokens containing `°` or `ø`
survive the command line. It sets the console input and output code pages to UTF-8,
so non-ASCII output displays correctly, and uses `std::filesystem::u8path` for
user-supplied paths. All numeric formatting and parsing uses the classic locale.

## Tests

`Tests/TheoryTest.cpp` is plain C++ with no JUCE dependency and covers parsing,
spelling, keys, voicing, and the render plan. `Tests/ProcessorTest.cpp` is a JUCE
console application covering transport behaviour, note lifecycle, parameters, state
round-trip, the editor, Regenerate, the Beats column, the drag-file writer and its
two modes, and MIDI export/read-back. `Tests/CliTest.cpp` covers the command-line
utility: option parsing, formatting, JSON, and REPL state.

Run all three suites through a build script:

```bash
./build.sh --test
```

```bat
build.bat test
```

Or run the already-built executables directly:

```bash
./build/Tests/TheoryTests.exe
./build/Tests/ProcessorTests.exe
./build/Tests/CliTests.exe
```

Each executable prints a summary such as `115 checks, 0 failures` for `TheoryTests`,
`233 checks, 0 failures` for `ProcessorTests`, and `204 checks, 0 failures` for
`CliTests`, and returns a non-zero exit code if any check fails. The build scripts
stop on the first failing suite. The build must produce no warnings from project
code under JUCE's recommended warning flags.

## Licensing

Chord Text links JUCE 8, which is available under the AGPLv3 or a commercial JUCE
license. Personal learning use without distribution is permitted under the AGPLv3.
Distributing a build requires a deliberate licensing decision: comply with the
AGPLv3 or obtain a commercial JUCE license. This repository does not grant a separate
license for its own source.

## Known limitations and out-of-scope features

Known limitations in v1:

- Windows x64 only; there are no macOS or Linux builds.
- Live generation requires host PPQ and tempo; without them the plugin stays silent
  and does not free-run.
- MIDI input is accepted for host compatibility but ignored.
- Slash bass and applied chords (`V/V`) are unsupported on Roman numerals.
- Parenthesised alterations are unsupported, and `Cb5`/`C#5` are read as root
  accidentals before the `5` quality; see
  [SYNTAX.md](SYNTAX.md#36-root-accidental-precedence-cb5).
- Each upper chord tone sounds once, but a slash bass may duplicate its pitch class
  an octave below (for example, `C/E`). There is no other octave doubling and no
  rootless voicing.
- The Degree column is best-effort for chromatic or ambiguous roots.
- `prepareToPlay` queues note-offs for pitches that were sounding and emits them at
  sample 0 of the next block. `releaseResources` cannot emit MIDI, so it only clears
  internal note tracking; the host is expected to flush sounding notes when it
  releases the plugin.
- The plugin reuses the host MIDI buffer by clearing it first. JUCE's VST3 wrapper
  normally preallocates enough capacity, but a zero-capacity host buffer may
  allocate on the first `MidiBuffer::addEvent`; strict no-allocation depends on the
  host providing capacity.
- Export uses the last tempo observed in `processBlock`. If a project is exported
  before the host has processed a block since loading, the file uses 120 BPM.
- Bitwig may reject an external file drag from a VST3 plugin window. If the drop is
  ignored, the file is still saved and **Reveal last clip** provides the fallback.
  Direct drop acceptance has not been verified in this environment.
- A drag clip has no tempo event, so Bitwig's project tempo is expected to govern
  playback. Bitwig may instead assume 120 BPM; this is host-dependent and
  unverified.
- Dragging writes a new file on every attempt under `Documents\ChordText\Clips`.
  Files accumulate, and there is no automatic cleanup in v1.
- There is no native Bitwig clip-creation API for a VST3 plugin. Chord Text writes
  a `.mid` file and starts an OS drag; it does not create or edit a Bitwig clip
  directly.
- The preview's bar count assumes 4/4 and is informational. The clip itself is
  beat-based, so a host in another meter can still use it.
- The editor is a fixed 760×640. The increased height has not been checked in
  Bitwig's plugin window.
- Host state restore is assumed to run on the message thread. If a host calls
  `setStateInformation` on another thread, the plugin defers the plan rebuild to
  the message queue, but the host is still responsible for serialising state
  changes.
- Only one progression is active at a time; there are no saved presets beyond the
  host project state.

Command-line utility limitations:

- Windows x64 only, consistent with the project.
- No audio preview; the tool reports text and writes MIDI files only.
- The REPL is line-oriented with no history, editing, or completion.
- `--bpm` defaults to 120 because there is no host tempo to observe.
- JSON in the REPL is one document per report, interleaved with human command
  feedback on stderr, so it is not a single JSON stream.
- The shared writer replaces the JUCE export implementation; parity is guarded by
  the existing read-back tests rather than a byte-for-byte comparison.

Explicitly out of scope for v1:

- standalone application, live MIDI-input triggering, MIDI Thru;
- key changes or modulation, secondary dominants;
- 11th and 13th chords, `6/9` chords;
- MPE and MIDI 2.0;
- dragging individual chords or table rows, dragging multiple loop repetitions, and
  a piano-roll preview;
- custom UI styling, a built-in synth, and audio output;
- automated CI.

## Manual DAW verification checklist

Automated tests cover parsing, voicing, transport, note lifecycle, state, MIDI
export, Regenerate, the Beats column, the drag-file writer, and the clip-preview
state. The following host checks require a VST3-compatible DAW and a human at the
keyboard. They are documented here but were **not** performed as part of this
repository's automated verification, so they remain unverified until someone runs
them.

- [ ] Route **Chord Text** to an instrument and hear the default progression.
- [ ] Change the text to `C, Cmaj7, Dmaj7, Emin9, Fmaj7` and confirm the notes.
- [ ] Change to `i-iv-V` in A minor and confirm Am, Dm, E.
- [ ] Test explicit durations, for example `Am7:2, Dm7:1.5, G7`.
- [ ] Switch between voice-led and root-position voicing and confirm audibly
      different, valid voicings.
- [ ] Start, stop, seek, loop, and bypass during playback; confirm there are no
      stuck notes.
- [ ] Switch keys and confirm Roman numerals resolve against the selected key while
      absolute chord symbols stay unchanged.
- [ ] Switch among the C2–C5, C3–C6, and C4–C7 ranges and confirm every generated
      note stays within the selected range.
- [ ] Change velocity and confirm generated notes use the new value.
- [ ] Toggle loop off and confirm the progression plays once, then toggle it on and
      confirm it loops; confirm no notes stick at either transition.
- [ ] Record generated notes into the piano roll and verify timing against the host
      grid.
- [ ] Export a `.mid`, import it, and compare it with live playback.
- [ ] Enter invalid text and confirm the last valid progression keeps playing with
      clear diagnostics.
- [ ] Confirm the learning table shows the expected degrees and spelled notes for
      the current key and voicing.
- [ ] Save and reload the project; confirm the text, key, mode, voicing, range,
      velocity, and loop setting restore.
- [ ] Confirm the Beats column shows `4` for chords without a `:N` suffix and the
      explicit value for chords with one.
- [ ] Change the text and press Regenerate; confirm the table updates immediately.
- [ ] Enter invalid text and press Regenerate; confirm the previous rows and plan
      stay and diagnostics appear.
- [ ] In Bitwig, drag the clip preview into the arranger or onto a track.
- [ ] Confirm a MIDI clip appears with the expected notes and loop length.
- [ ] Confirm the dragged clip follows the project tempo.
- [ ] If the drop is ignored, click Reveal last clip and drag the file from
      Explorer into Bitwig.
- [ ] Confirm Export `.mid` still works and that the exported file retains tempo
      and 4/4 metadata.

`pluginval` is optional. If it is installed, run it against the built VST3 and fix
anything that affects this plugin; it is not required to build or use Chord Text.

## Repository layout

```text
CMakeLists.txt              plugin, test executables, JUCE subdirectory
README.md                   this document
SYNTAX.md                   canonical chord and progression syntax guide
build.sh / build.bat        configure/build/test wrappers
Source/Theory/              JUCE-free notes, keys, chords, parsers, voicing, render plan,
                            MIDI file writer
Source/Plugin/              processor (transport, state, export, drag files), editor,
                            clip drag component
Source/Cli/                 command-line utility (options, report, session, main)
Tests/                      TheoryTests, ProcessorTests, and CliTests
docs/superpowers/specs/     design specification
libs/JUCE/                  pinned JUCE checkout (ignored by Git)
build/                      generated build tree (ignored by Git)
```
