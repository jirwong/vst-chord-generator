# Chord Text

Chord Text is a Windows x64 VST3 MIDI generator. Type a chord progression as chord
symbols or Roman numerals, and the plugin plays it in sync with the host transport
or exports it as a Standard MIDI File. The editor also shows a learning table with
each chord's functional degree and spelled chord tones.

The plugin produces MIDI only. It has no audio output and no built-in synth, so it
must be routed to an instrument in the host.

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
./build.sh --test   # build, then run TheoryTests and ProcessorTests
```

Command Prompt:

```bat
build.bat
build.bat test
```

Both scripts return a non-zero exit code if configuration, the build, or either test
suite fails.

### Build outputs

- VST3 bundle: `build/ChordText_artefacts/Release/VST3/Chord Text.vst3`
- Binary inside the bundle:
  `build/ChordText_artefacts/Release/VST3/Chord Text.vst3/Contents/x86_64-win/Chord Text.vst3`
- Test executables: `build/Tests/TheoryTests.exe` and
  `build/Tests/ProcessorTests.exe`

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
   updates shortly after typing stops.
4. Use the key, mode, voicing, range, velocity, and loop controls to shape the
   output.
5. Read the learning table for each chord's degree and spelled notes, then export a
   `.mid` file if desired.

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

## Syntax reference

### Progressions and separators

A progression is a sequence of chord tokens. Tokens may be separated by any of:

- commas: `C, Am7, F, G`
- spaces or tabs: `C Am7 F G`
- newlines:

  ```text
  C
  Am7
  F
  G
  ```

- vertical bars: `C | Am7 | F | G`
- hyphens: `C-Am7-F-G`

Separators may be mixed. Repeated separators and leading or trailing separators are
ignored. `|` and `-` are visual separators only; they do not force a bar boundary
and do not change timing.

Empty text, or text containing only separators, is valid and produces an empty plan
with no notes and no errors. Absolute and Roman-numeral tokens may be mixed in one
progression.

> A hyphen is **not** a minor-quality alias. `C-E-G` means three chords (C, E, G),
> not C minor. Write `Cm` or `Cmin` for C minor.

### Durations

Every chord lasts 4 beats by default. Add `:N` to set the duration in beats:

```text
Cmaj7:2, Am7:1.5, Dm7:8, G7
```

- `N` must be a positive, finite number; fractional values are allowed.
- Zero, negative values, NaN, infinity, and malformed values are errors.
- Durations affect both live playback and export. The loop length is the sum of all
  chord durations.
- A chord's note-on occurs at its start beat and its note-off at start + duration.

### Absolute chord symbols

```text
chord       := root quality? alteration* ("/" bass)?
root        := [A-G] ("#" | "b")?
bass        := [A-G] ("#" | "b")?
alteration  := ("b" | "#") ("5" | "9")
```

Root and bass letters are uppercase `A` through `G`. Root spelling from the input is
preserved: `C#` major is spelled C#–E#–G#, and `G#maj7` contains F##. Octave and MIDI
pitch are chosen by the voicer.

#### Supported qualities and aliases

| Category | Spellings |
|---|---|
| Major triad | *(empty)* |
| Minor triad | `m`, `min` |
| Diminished triad | `dim`, `o`, `°` |
| Augmented triad | `aug`, `+` |
| Suspended | `sus2`, `sus4` |
| Power chord | `5` |
| Sixth | `6`; minor `m6`, `min6` |
| Seventh | `7`, `maj7`, `M7`, `m7`, `min7`, `mMaj7`, `minMaj7`, `dim7`, `o7`, `°7`, `m7b5`, `ø7`, `7sus4` |
| Ninth | `9`, `maj9`, `M9`, `m9`, `min9`, `add9`, `9sus4` |

Qualities are matched longest-first, so `maj7` is not read as `m` plus `aj7`.

#### Alterations

`b5`, `#5`, `b9`, and `#9` may follow the quality, in any order and in any
combination:

```text
C7b9
C7#9
Cm7b5
C7#5b9
```

`b5`/`#5` alter the fifth and `b9`/`#9` alter the ninth; if the base quality does
not contain that degree, the altered tone is added. `add9` adds the ninth without a
seventh.

#### Slash bass

Add `/` and a bass note to fix the lowest note:

```text
C/E
Am7/G
F#m7b5/C
```

The slash bass is always the lowest note; the remaining chord tones are voiced above
it. Slash bass is supported on absolute chords, not on Roman numerals.

#### Root accidental precedence: `Cb5`

The parser consumes a root accidental immediately after the root letter, before
reading the quality. Therefore `Cb5` is the **C-flat power chord** (Cb plus its
fifth), not "C with a flat fifth". Likewise `C#5` is the C-sharp power chord.

Write alterations only after an explicit quality, for example `C7b5`, `Cm7b5`, or
`C7#5`. Parenthesised forms such as `C(b5)` are not supported.

#### Unsupported absolute tokens

Unknown or ambiguous tokens are errors, not guesses. Examples: `C99`, `C11`, `C13`,
`C6/9`, and `H`.

### Roman numerals

```text
degree      := accidental? numeral quality? alteration*
accidental  := "b" | "#"
numeral     := "I" | "II" | "III" | "IV" | "V" | "VI" | "VII"
```

- Case sets the default triad quality: uppercase is major, lowercase is minor.
- `°` or `o` makes the triad diminished; `+` makes it augmented.
- An explicit quality suffix overrides the case default, using the same vocabulary as
  absolute chords. Bare extensions follow the case: lowercase `ii7` is a minor
  seventh, while uppercase `V7` is a dominant seventh.
- `b` and `#` before the numeral alter the scale-degree root chromatically.
- Slash bass and applied/secondary numerals such as `V/V` are not supported in v1.

Examples in C major:

```text
I       -> C
ii      -> Dm
V7      -> G7
IVmaj7  -> Fmaj7
bVII    -> Bb
#iv     -> F#m
```

Examples in A minor:

```text
i       -> Am
iv      -> Dm
V       -> E
V7      -> E7
vii°    -> Gdim
#vii°   -> G#dim
```

Minor keys use natural-minor spelling for scale degrees. Chromatic notes come from
accidentals or from the chord quality itself. In A minor, `V` is E–G#–B, with the G#
spelled correctly; `vii°` uses the natural-minor G.

### Key selector

The key selector supplies the tonic spelling and the major/minor mode. It always
matters for Roman numerals and for the Degree column of the learning table. It does
**not** transpose or alter absolute chord symbols: `C` is C major regardless of the
selected key. Selectable tonics are C, C#, Db, D, D#, Eb, E, F, F#, Gb, G, G#, Ab, A,
A#, Bb, and B, in major or minor.

### The learning table

The table shows the last valid progression:

| Chord | Degree | Notes |
|---|---|---|
| Cmaj7 | Imaj7 | C E G B |
| Am7 | vi7 | A C E G |
| Dm7 | ii7 | D F A C |
| G7 | V7 | G B D F |

The Notes column follows the current voicing and range and uses each chord's spelled
tones, so Roman numerals inherit the key's spelling. The Degree column is a
normalized functional label; absolute chords are labeled best-effort relative to the
selected key using the root letter and accidental (not by nearest pitch class).
Chromatic or ambiguous roots use a best-effort `b`/`#` label. The row for the
currently playing chord is highlighted during playback.

### Errors and diagnostics

Every parse error identifies the failing token, a zero-based character offset into
the progression text, and a short message. The editor shows every diagnostic, one
per line, in a scrollable area below the learning table:

```text
Error at character 12: unknown extension "99" (Emin99)
```

If any token is invalid, no new plan is published. The last valid progression keeps
playing, and the table continues to show it. Export is disabled while errors are
present.

## Controls and saved state

| Control | Default | Notes |
|---|---|---|
| Progression text | `Cmaj7, Am7, Dm7, G7` | Multi-line, parsed 150 ms after the last edit |
| Key | C | 17 tonic spellings |
| Mode | Major | Major or minor |
| Voicing | Voice-led | Voice-led or Root position |
| Range | C3–C6 | C2–C5, C3–C6, or C4–C7 |
| Velocity | 100 | 1–127 |
| Loop | On | On or off |

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

Click **Export .mid** to write one complete loop of the current progression. The
writer uses the same render plan as live playback:

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

## Tests

`Tests/TheoryTest.cpp` is plain C++ with no JUCE dependency and covers parsing,
spelling, keys, voicing, and the render plan. `Tests/ProcessorTest.cpp` is a JUCE
console application covering transport behaviour, note lifecycle, parameters, state
round-trip, the editor, and MIDI export/read-back.

Run both suites through a build script:

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
```

Each executable prints a summary such as `87 checks, 0 failures` for `TheoryTests`
and `136 checks, 0 failures` for `ProcessorTests`, and returns a non-zero exit code
if any check fails. The build scripts stop on the first failing suite. The build must
produce no warnings from project code under JUCE's recommended warning flags.

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
  accidentals before the `5` quality (see above).
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
- Host state restore is assumed to run on the message thread. If a host calls
  `setStateInformation` on another thread, the plugin defers the plan rebuild to
  the message queue, but the host is still responsible for serialising state
  changes.
- Only one progression is active at a time; there are no saved presets beyond the
  host project state.

Explicitly out of scope for v1:

- standalone application, live MIDI-input triggering, MIDI Thru;
- key changes or modulation, secondary dominants;
- 11th and 13th chords, `6/9` chords;
- MPE and MIDI 2.0;
- drag-out MIDI clips and a piano-roll preview;
- custom UI styling, a built-in synth, and audio output;
- automated CI.

## Manual DAW verification checklist

Automated tests cover parsing, voicing, transport, note lifecycle, state, and MIDI
export. The following host checks require a VST3-compatible DAW and a human at the
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

`pluginval` is optional. If it is installed, run it against the built VST3 and fix
anything that affects this plugin; it is not required to build or use Chord Text.

## Repository layout

```text
CMakeLists.txt              plugin, test executables, JUCE subdirectory
README.md                   this document
build.sh / build.bat        configure/build/test wrappers
Source/Theory/              JUCE-free notes, keys, chords, parsers, voicing, render plan
Source/Plugin/              processor (transport, state, export) and editor
Tests/                      TheoryTests and ProcessorTests
docs/superpowers/specs/     design specification
libs/JUCE/                  pinned JUCE checkout (ignored by Git)
build/                      generated build tree (ignored by Git)
```
