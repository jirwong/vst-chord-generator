# Chordscript

Chordscript is a Windows x64 VST3 MIDI generator. The host lists it as **Chord Text**.
Type a chord progression as chord symbols, Roman numerals, numeric chords, or
custom note lists in square brackets.
The plugin plays it in sync with the host transport, or
exports it as a Standard MIDI File. You can also drag one loop from the editor into
a DAW such as Bitwig as a MIDI clip. The editor shows chord cards. Each card
shows the functional degree or custom formula, the voiced chord tones, an optional
root bass note, and the written length plus resolved beat count for each chord.

The plugin produces MIDI only. It has no audio output and no built-in synth. You
must route it to an instrument in the host.

For the full syntax reference, see [SYNTAX.md](SYNTAX.md).

## Prerequisites

- Windows 10 or 11, 64-bit.
- Visual Studio 2022 Build Tools with the "Desktop development with C++" workload
  (MSVC 14.44 or compatible).
- CMake 3.22 or newer. The copy shipped with Build Tools is sufficient. The build
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

Chord Text is a VST3 MIDI generator (`IS_MIDI_EFFECT TRUE`, MIDI input and output
enabled, no audio buses). The plugin ignores incoming MIDI in v1.

### Install

Either copy the whole `build/ChordText_artefacts/Release/VST3/Chord Text.vst3`
bundle into the system VST3 folder:

```text
C:\Program Files\Common Files\VST3
```

Or run the included installer from an Administrator Command Prompt:

```bat
install-vst3.bat
```

It copies the Release bundle from `build\ChordText_artefacts\Release\VST3` into
`C:\Program Files\Common Files\VST3`. Close any DAW using the plugin before
running it.

Or leave it in the build tree and add the folder as a plugin search path:

1. In the DAW's plugin settings, add
   `...\chord-generator\build\ChordText_artefacts\Release\VST3`
   to the additional VST3 search paths.
2. Start a plugin scan and confirm that **Chord Text** appears as a MIDI generator.

### Route to an instrument

The plugin must drive another instrument. It makes no sound by itself. Two common
routes are:

- **Host plugin chain:** insert **Chord Text** before an instrument (for example a
  synth or sampler) in the same channel or instrument chain. The plugin's MIDI
  output then feeds the instrument's MIDI input directly.
- **MIDI output ports:** load Chord Text in one channel, then route its MIDI output
  port to a second channel or instrument whose MIDI input port matches.

The exact routing steps depend on the DAW and its plugin wrapper. In-host routing is
in the manual checklist below. It has **not** been verified in this environment.

## Quick start

1. Load Chord Text in a channel and route it to an instrument.
2. Press Play in the DAW. The default progression `Cmaj7, Am7, Dm7, G7` plays four
   beats per chord and loops.
3. Type a progression in the text box. The plugin parses the text 150 ms after your
   last edit, so playback updates soon after you stop typing. Press **Regenerate**
   to apply the current text immediately.
4. Use the key, mode, voicing, range, velocity, default length, bass, and loop
   controls to shape the output.
5. Left-press a chord card to hear it, or right-click to hold it. Release the left
   button or right-click the held card again to stop.
6. Read the chord cards for the degree, spelled notes, and length of each
   chord, then drag the clip preview into the DAW or export a `.mid` file.

Live generation needs host playhead data with a PPQ position and tempo. The status
line shows one of these messages:

- `Transport playing` — the transport is playing.
- `Transport stopped` — a playhead position exists, but the transport is not
  playing.
- `No transport available` — the host supplies no usable position.

With no usable position, the plugin generates no notes and does not free-run.
Manual chord preview is the exception: it can sound while the transport is stopped
or no playhead is available, as long as the host keeps processing the plugin.

### Musical lengths and host meter

Use **DEFAULT LENGTH** to choose the length of chord tokens without a suffix. It is
saved with the project and starts at 4 beats. Write an explicit suffix when a step
needs a different length, for example `C:1/8D, Dm:2beats, G:1bar`.

One internal beat is one quarter note in every meter. A bar length follows the
current DAW time signature: one bar is three beats in 6/8 and four beats in 4/4.
The sequence status shows the current host meter, or `4/4 fallback` when the host
does not supply one. A meter change rebuilds bar-based lengths for the current song
position. Note values and explicit beat values keep their duration.

The cards show the written or inherited length and resolved beats. **DEFAULT** means
the step inherits DEFAULT LENGTH. Full syntax, including dotted, triplet, beat, and
bar forms, is in [SYNTAX.md](SYNTAX.md#2-musical-lengths).

The progression is anchored to song position 0. If you start playback or seek into
the middle of a chord, the plugin plays that chord from the start of the block. The
progression loops with a length equal to the sum of the chord lengths. Loop is on
by default; with loop off, the progression plays once. The plugin releases active
notes before it emits new notes after a stop, a seek, a bypass, a plan change, or a
loop wrap. When the host calls `prepareToPlay` again, the plugin queues the sounding
pitches for release and emits their note-offs at sample 0 of the next block, before
any new events. `releaseResources` cannot emit MIDI, so it only clears the internal
note tracking; the host must flush sounding notes when it releases the plugin.

## Previewing chords

Each chord card can be auditioned directly while the plugin is routed to an
instrument. Preview sends the same generated pitches as normal playback; it does not
add a synth.

- Left-press a card to start that chord immediately. Release the left mouse button
  to stop it. A matching release outside the card or the plugin window still stops
  the preview.
- Right-click a card to hold it. Right-click the same held card again to release it.
  Ordinary focus loss does not release a hold, so you can keep using the DAW while a
  chord is held.
- Only one manual preview sounds per plugin instance. Choosing another card replaces
  the first without layering. Left-pressing another card replaces a held chord;
  releasing the left button does not restore the old hold.
- Pressing the sounding card again releases and retriggers it, even when the pitches
  are identical.
- Preview uses the card's generated pitches, channel 1, and the current velocity.
  The card's resolved length does not limit preview.
- During playback, manual preview replaces the progression's output while the host
  timeline keeps moving. When preview ends, normal output resumes at the current
  host position; it does not restart the progression. Releasing exactly on a chord
  boundary starts the newly active chord once.
- Preview is transient and per instance. It is not stored in project state, APVTS,
  UI preferences, exported MIDI, or drag clips.

Preview works without host PPQ or tempo, including with the transport stopped, but
it needs the host to keep calling the plugin's process callback. A suspended or
offline plugin cannot emit MIDI. If the host stops processing, sounding notes remain
until processing resumes or the host flushes them; the same host-flush limitation
applies to normal playback.

Preview stops immediately when you begin editing the progression text (before the
150 ms parse), when key, mode, scale mode, voicing, range, velocity, bass, or Loop
changes, when project state is restored, when the editor closes, when the plugin is
bypassed, and when the host resets processing. A cancelled hold does not restart automatically
after a valid edit, unbypass, editor reopen, project load, or processing reset. A new
gesture is required. Changing UI scale, exporting MIDI, and dragging a clip do not
cancel preview. Transport start, stop, seek, loop wrap, and tempo changes do not
cancel or retrigger it either.

The chord card shows `PREVIEW` for a momentary preview, a pin and `HELD` for a
latched preview, and `PLAYING` only for normal progression output. Normal playback
highlighting is suppressed while a manual preview is sounding.

## Regenerate and automatic parsing

**Regenerate** is on the progression header row. When you click it, the plugin
cancels any pending debounce, parses the text in the editor immediately, and
rebuilds the playback plan and chord cards from the current text and settings. It
rebuilds the plan even if the text did not change.

Automatic parsing stays on: the plugin parses the text 150 ms after your last edit.
Regenerate is an explicit alternative to waiting for that timer, not a replacement
for it.

If the text is not valid, Regenerate does not replace the last valid plan or chord
cards. The current progression keeps playing and the diagnostics area shows the
errors. An empty progression is valid; Regenerate then clears the cards and
disables export and dragging.

## Syntax reference

The full, canonical syntax guide is [SYNTAX.md](SYNTAX.md). It covers
progressions and separators, musical lengths, absolute chords and aliases,
alterations, slash bass, Roman numerals, custom note lists, added root bass, keys,
grammar, diagnostics, and common mistakes.

Quick examples:

```text
Cmaj7, Am7, Dm7, G7
i-iv-V
C:1/8D, Am7:2beats, F:1bar, G:1/8T
C/E, F#m7b5/C
1, 6, 4, 5
1(7), 2(7), 5(7):1/8T
[1,2,4,5]
1, [1,3,5,F]:2, 6, 5
```

Every chord uses DEFAULT LENGTH when it has no suffix. Separators can be mixed. You
can use absolute chords and Roman numerals in the same progression. The plugin editor and
`chordtext-cli` share the parser and pitch resolver, so the same text and settings
produce the same notes in both.
To check syntax without a DAW, run `chordtext-cli "C, H"`.

The key selector matters for Roman numerals, numeric chords, and degree labels. It does not
transpose absolute chords.

Numbers select chords from the chosen scale. In C Major, `2` is Dm and `2(7)` is
Dm7. In A Minor, `5` is Em. Use `5maj` for E or `5dom7` for E7.
Separate the degrees: `1-6-4-5` is valid, but `1645` is invalid.

Write a custom chord as a comma-separated list in square brackets. In C Major,
`[1,2,4,5]` gives C, D, F, G. The first entry sets the root. You can mix scale
degrees and named notes, for example `[1,3,5,F]`. A custom card shows its formula
and octave-aware notes. Hover over a card to read the full text when a line is too
long to fit.

The **BASS** menu adds the custom chord's root, or another chord's voiced root,
one, two, or three octaves lower. Its default is **Off**. For example, BASS `1`
adds the root one octave down. Bass uses the chord's timing and velocity in
playback, preview, export, and drag clips. The setting belongs to the plug-in
instance and is saved in the DAW project.

The Mode selector supports Major (Ionian), Minor (Aeolian), Dorian, Phrygian,
Lydian, Mixolydian, Locrian, Harmonic Minor, and Melodic Minor. Melodic Minor
always uses its ascending form. The scale summary shows the current tonic and
seven scale notes. See [numeric syntax](SYNTAX.md#52-numeric-scale-chords) and
[quality overrides](SYNTAX.md#53-explicit-numeric-chord-quality) for examples.

### Chord cards

The cards show the last valid progression. Each card shows the chord, degree or
custom formula, notes, optional bass note, written or selected length, and resolved
beat count. A card has **DEFAULT** only when that step inherits DEFAULT LENGTH.
The Notes line follows the current voicing and range. It uses each chord's spelled
tones, so Roman numerals inherit the key's spelling. The Degree line is a normalized functional label.
Absolute chords are labeled best-effort relative to the selected key, using the root
letter and accidental (not the nearest pitch class). Chromatic or ambiguous roots use
a best-effort `b`/`#` label. The plugin highlights the card for the currently playing
chord during playback, marks a sounding manual preview as `PREVIEW` or `HELD`, and
marks normal output as `PLAYING`.

Numeric cards also show the numeric degree, for example `Degree 2 · ii7`.
Cards show the last valid progression with reduced brightness while input is invalid.
Custom cards show a formula in place of a degree label. When bass is on, they show
the resolved bass note on a separate line. The full formula and notes are available
in the card tooltip.

### Errors and diagnostics

Every parse error identifies the failing token, a zero-based character offset into
the progression text, and a short message. The editor lists errors below the
progression input. If any token is invalid, no new plan is published. The last valid
progression keeps playing. Its cards stay visible, and export and drag are disabled.
See [SYNTAX.md](SYNTAX.md#6-errors-and-diagnostics) for the exact format and
examples.

## Controls and saved state

| Control | Default | Notes |
|---|---|---|
| Progression text | `Cmaj7, Am7, Dm7, G7` | Multi-line, parsed 150 ms after the last edit |
| Regenerate | — | Parses the current text and rebuilds immediately |
| Key | C | 17 tonic spellings |
| Mode | Major (Ionian) | Nine scales; see the syntax guide |
| Voicing | Voice-led | Voice-led or Root position |
| Range | C3–C6 | C2–C5, C3–C6, or C4–C7 |
| Velocity | 100 | 1–127 |
| Default Length | 4 beats | Used by chord tokens without an explicit suffix; 19 musical choices |
| Loop | On | On or off |
| Bass | Off | Add the chord's voiced root 1, 2, or 3 octaves lower |
| Chord preview | — | Left-press to preview, release to stop; right-click to hold, right-click again to release |
| Drag to DAW | — | Drags one loop as a `.mid` file; enabled for valid, non-empty progressions |
| Reveal file | — | Opens Explorer at the most recent successful export or drag file |
| UI Scale | 100% | Footer dropdown: 75%, 100%, 125%, or 150%; saved locally |

All notes are sent on MIDI channel 1. Root-position voicing places the root at the
lowest octave that fits the range. It stacks the remaining tones above the root.
Voice-led voicing keeps the first chord in root position. It then chooses the
inversion and octave placement that minimizes movement from the previous chord, and
stays within the selected range. A slash bass is part of the chord voicing and is
the lowest chord note in both modes. The optional added root bass is placed below
the chord's voiced root; it may be higher than another low chord tone. Ordinary
upper voices contain each chord tone once. A slash bass may duplicate its pitch
class an octave below the upper chord tones (for example, `C/E` sounds two E's).
Custom lists can request different octaves, and the optional root bass can add a
lower root. Identical final MIDI pitches sound once.

The project state saves the progression text, key, mode, voicing, range, velocity,
default length, loop, and bass octave setting. It uses stable parameter IDs (`key`, `mode`,
`voicing`, `range`, `velocity`, `loop`, `scaleMode`, `bassOctaves`, `defaultLength`) plus the
`progressionText` property. Restoring a project rebuilds the plan but does not start
playback. Hosts must restore state on the message thread.
Projects saved before Default Length existed restore a four-beat default.
If a host restores state on another thread, the plugin defers the plan rebuild to the
message queue.

The editor's Mode selector uses the new host parameter **Scale Mode** (`scaleMode`).
Use this parameter to automate all nine scales. The original **Mode** parameter
keeps its two Major/Minor values for existing automation. Old projects restore
their original selection. New projects save the nine-choice selection.

### UI scaling

Use **UI SCALE** in the footer to change the interface size. Text, controls, and
the plugin window scale together. The layout keeps its proportions.

| Scale | Window size before host display scaling |
|---|---|
| 75% | 720 × 488 |
| 100% | 960 × 650 |
| 125% | 1200 × 813 |
| 150% | 1440 × 975 |

The current window changes immediately. Other open windows keep their size.
Each editor reads the saved preference when it opens, including editors in other DAWs.

The preference is stored for your Windows account in
`%LOCALAPPDATA%\Chordscript\ui-settings.json`. It is separate from project state
and is not an automation parameter. A missing or invalid preference uses 100%.
Loading a project does not replace the local preference or resize an open editor.

If saving fails, the current window keeps the selected size. The footer shows
`UI scale was not saved`. New editors continue to use the last saved value.
User zoom combines with the DAW's display scaling. It does not change Windows file dialogs.

## MIDI export

Click **Export MIDI** to write one complete loop of the current progression. Export
is separate from dragging. Unlike a drag file, an exported file always includes the
tempo and effective time-signature metadata. The writer uses the same render plan as live
playback:

- the last host tempo observed during processing, or 120 BPM before any tempo has
  been observed;
- the current host time signature, or the 4/4 fallback;
- track name `Chord Text`;
- 960 ticks per quarter note;
- deterministic event order with note-offs before note-ons at the same tick;
- channel 1 note events.

`AudioPlayHead::getPosition()` is only valid inside `processBlock`. The processor
therefore caches the last valid host tempo in an atomic. Export never queries the
playhead. Export still works with no playhead, at 120 BPM until the host has
processed a block.

The button is disabled when the text has errors or the progression is empty. It is
also disabled while a file chooser is open. Export runs on the message thread. It
appends `.mid` if the chosen filename lacks it. The status line reports the file name
or the failure reason.

Export retains the tempo event (the last observed host tempo, or 120 BPM) and the
effective time signature. Only the drag path omits them.

## Dragging clips into a DAW

The editor contains a clip-shaped **Drag to DAW** preview. It shows the loop length
as the sum of the resolved chord lengths, for example `3 beats · 1 bar (6/8)`. A
left-button drag starts only after a small mouse movement. A click therefore does not
create a file.

The preview is enabled after the last successful parse, when that plan contains at
least one note. While a debounced edit is pending, the preview shows the last parsed
text, not the characters just typed. An empty progression also disables the preview.
While the preview is disabled, it shows `Fix errors to drag`, or
`Enter a progression to drag` when the progression is empty. A drag start first
applies a pending edit on the same path as Regenerate when the text has changed. An
unchanged text is not rebuilt, so dragging does not cancel an active chord preview.
If parsing fails, no file is created and the diagnostics are shown.

A drag writes exactly one loop cycle at the current key, mode, voicing, range, and
velocity settings. The file uses the same notes, 960 ticks per quarter note, MIDI
channel 1, track name `Chord Text`, and note-off-before-note-on ordering as live
playback and export. It deliberately contains **no tempo meta event and no
time-signature meta event**. The DAW's project tempo and meter therefore govern the
clip. If the host instead assumes 120 BPM, that is a host-behaviour risk noted below.

### File location and lifetime

Drag files are written to:

```text
Documents\ChordText\Clips
```

The plugin creates the folder if it is missing. Each file is named
`ChordText-YYYYMMDD-HHMMSS.mid` using local time, so repeated drags do not overwrite
earlier clips. If two drags happen in the same second, the plugin adds a numeric
suffix to the timestamped name instead of overwriting the existing file. Files are
never deleted automatically in v1. A clip may already be referenced by a saved DAW
project, and removal of its source would break that project.

### Fallback: Reveal last clip

Every started drag leaves a real file in the clips folder, whether or not the DAW
accepts the drop. The plugin cannot detect whether the drop succeeded. The status
line therefore reports the saved filename and that the plugin requested the drag, for
example `Clip saved: ChordText-20261022-143015.mid; drag requested.` If the drag
could not be queued, it reports
`Clip saved: ChordText-20261022-143015.mid; drag could not be queued.` Neither
message claims that the DAW accepted the drop.

**Reveal last clip** is next to **Export .mid**. It is enabled once a drag file has
been created in the current plugin session. It opens Explorer with that file
selected. The last drag path is session-only and is not saved with the project.
Reveal therefore starts disabled after reloading, even if earlier clip files still
exist in the folder. If Bitwig ignores the external drag, click Reveal last clip and
drag the file from Explorer into Bitwig.

### Bitwig notes

Bitwig receives the plugin as a VST3 MIDI generator. Load the plugin, then route its
MIDI output to an instrument as usual. The drag writes a file and starts an
operating-system file drag. It does not create a Bitwig clip through a native API. A
VST3 plugin has no such API, and Bitwig does not expose one for this workflow. The
resulting clip should follow the Bitwig project tempo because the file has no tempo
event. This behaviour is host-dependent and has not been verified here. Use the
checklist below.

## Command-line utility

The build also produces a standalone console program, `build/Cli/chordtext-cli.exe`.
It parses a progression and reports what the plugin would do — without a DAW, a VST3
host, or JUCE. It supports one-shot checks, piped stdin, and an interactive REPL. It
can also write a Standard MIDI File through the same shared writer as the plugin.

```text
chordtext-cli [options] [progression]
```

There is at most one positional argument, the progression text. With a progression
argument, the utility runs once and exits. It ignores stdin. With no argument and a
non-interactive (piped) stdin, it reads all of stdin as the progression. With no
argument and an interactive terminal, it starts the REPL. `--` ends option parsing,
so a progression that begins with `-` is taken literally.

One-shot:

```bash
./build/Cli/chordtext-cli.exe --key A --mode minor "i-iv-V"
./build/Cli/chordtext-cli.exe --key C --mode major --bass 1 "1, [1,3,5,F]:2, 6, 5"
./build/Cli/chordtext-cli.exe --default-length 1/8D --time-signature 6/8 "1, 4, 5:1bar"
```

```text
Progression: i-iv-V
Key: A minor · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off · Default length: 4beats · Time signature: 4/4

Chord  Degree  Notes   Length  Beats  MIDI
Am     i       A C E   4beats  4      57 60 64
Dm     iv      A D F   4beats  4      57 62 65
E      V       G# B E  4beats  4      56 59 64
```

Piped stdin:

```bash
echo "C, H, C99" | ./build/Cli/chordtext-cli.exe
```

```text
Progression: C, H, C99
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off · Default length: 4beats · Time signature: 4/4

2 errors:
Error at character 3: unknown chord root (H)
Error at character 6: unknown extension "9" (C99)
```

(exit code 1)

REPL (interactive terminal only): run `chordtext-cli` with no arguments. The banner
prints the current settings, and the prompt is `chord> `. A session looks like:

```text
Chord Text CLI — :help for commands
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off · Default length: 4beats · Time signature: 4/4

chord> Cmaj7
Progression: Cmaj7
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off · Default length: 4beats · Time signature: 4/4

Chord  Degree  Notes    Length  Beats  MIDI
Cmaj7  Imaj7   C E G B  4beats  4      48 52 55 59
chord> :key A
Key: A major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off · Default length: 4beats · Time signature: 4/4
chord> :quit
```

The full command list is under [REPL](#repl).

### Options

| Option | Values | Default |
|---|---|---|
| `--key <tonic>` | `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B`, case-insensitive | `C` |
| `--mode <mode>` | Nine scale names; case-insensitive; `ionian`/`aeolian` aliases | `major` |
| `--voicing <voice-led\|root-position>` | case-insensitive; `root` is an alias for `root-position` | `voice-led` |
| `--range <C2-C5\|C3-C6\|C4-C7>` | case-insensitive | `C3-C6` |
| `--velocity <1-127>` | integer | `100` |
| `--bpm <4-999>` | finite number in range | `120` |
| `--default-length <length>` | Any valid musical length | `4beats` |
| `--time-signature <N/D>` | Numerator 1–255; denominator 1, 2, 4, 8, 16, 32, or 64 | `4/4` |
| `--bass <off\|1\|2\|3>` | Add the voiced chord root 1, 2, or 3 octaves down | `off` |
| `--json` | flag | off |
| `--out <path>` | file path | off |
| `--help` | flag | — |

`--default-length` accepts the same grammar as a progression suffix. `--time-signature`
resolves `bar` lengths and sets metadata in an exported file. `--bpm` only affects a
written MIDI file. It is reported in JSON, but it has no effect on the table. Options accept both
`--name value` and `--name=value`. Boolean flags are the exception: they reject an
attached value. If an option is repeated, the last occurrence wins. Unknown options,
missing values, and invalid values are usage errors.

### Human output

A successful report prints a `Progression:` header, a settings line, and a table. The
header collapses runs of whitespace and newlines to single spaces, or shows `(empty)`
for empty text.

```text
Progression: Cmaj7, Am7, Dm7, G7
Key: C major · Voicing: root-position · Range: C2-C5 · Velocity: 100 · Bass: Off · Default length: 4beats · Time signature: 4/4

Chord  Degree  Notes    Length  Beats  MIDI
Cmaj7  Imaj7   C E G B  4beats  4      36 40 43 47
Am7    vi7     A C E G  4beats  4      45 48 52 55
Dm7    ii7     D F A C  4beats  4      38 41 45 48
G7     V7      G B D F  4beats  4      43 47 50 53
```

Columns are left-aligned with two spaces between them. The `Notes` column follows
the current voicing and matches the plugin's chord cards. An inversion therefore
appears in voicing order. A custom chord's notes include octave names. The `MIDI`
column lists all final pitches, including an added bass. When bass is enabled, the
`Bass` column shows its octave-aware note name. The column is not shown when bass is
Off. `Length` is the canonical suffix or selected default. `Beats` is the resolved
quarter-note duration and uses compact formatting. An empty progression is valid and prints
`Empty progression (no chords).`.

Diagnostics replace the table and use the editor's exact wording, one error per line
with the token's character offset:

```text
Progression: C, H, C99
Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off

2 errors:
Error at character 3: unknown chord root (H)
Error at character 6: unknown extension "9" (C99)
```

The count line is `1 error:` or `N errors:`. The parenthetical token is omitted when
the token is empty. The CLI reports only the current input. It never shows a stale
table from an earlier valid progression.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Progression parsed successfully (including an empty progression) and any requested file was written |
| 1 | The progression has parse or pitch-resolution errors |
| 2 | Usage error, or a requested file could not be written |

In one-shot mode, results go to stdout and usage errors go to stderr. I/O errors go
to stderr in human mode, and into the JSON `error` field in JSON mode. A REPL session
always exits 0, even after earlier parse errors.

### REPL

Running `chordtext-cli` with no arguments in an interactive terminal starts a REPL.
The banner prints a current-settings line and a hint to use `:help`. The prompt is
`chord> `. Settings stay sticky across lines until you change them. A non-empty line
that does not start with `:` is parsed as a progression and reported with the current
settings. The REPL ignores a blank line. A parse error does not exit the REPL.

| Command | Behaviour |
|---|---|
| `:key <tonic>` | Set the key tonic; same validation as `--key` |
| `:mode <mode>` | Set one of the nine scales |
| `:voicing <voice-led\|root-position>` | Set the voicing mode |
| `:range <C2-C5\|C3-C6\|C4-C7>` | Set the pitch range |
| `:velocity <1-127>` | Set the velocity |
| `:bpm <4-999>` | Set the MIDI tempo |
| `:default-length <length>` | Set the inherited chord length |
| `:time-signature <N/D>` | Set the meter used by bar lengths and MIDI export |
| `:bass <off\|1\|2\|3>` | Add the voiced chord root 1, 2, or 3 octaves down |
| `:json <on\|off>` | Switch report formatting between human and JSON |
| `:export <path>` | Write the last successfully parsed progression with the current settings |
| `:help` | List commands and current settings |
| `:quit`, `:q` | Exit |

After a successful setting change, the command prints the current-settings line. An
invalid command or value prints a short error and keeps the previous value. `:export`
with no valid progression yet, or when current settings make it invalid, prints an
error and writes nothing. EOF (Ctrl+Z on
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
    "bpm": 120,
    "bassOctaves": 0,
    "defaultLength": "4beats",
    "timeSignature": {"numerator": 4, "denominator": 4}
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
      "bassMidi": null,
      "bassNote": null,
      "startBeat": 0,
      "durationBeats": 4,
      "lengthExpression": "4beats",
      "usesDefaultLength": true
    }
  ],
  "midiFile": null,
  "error": null
}
```

This example was generated with `--voicing root-position --range C2-C5`. The default
settings produce `voice-led` and `C3-C6` (and the corresponding `voicing` numbers)
instead.

Field meanings:

- `settings` holds the effective settings as canonical values (`key` as written in
  the option list, `mode` as a mode name, `voicing` as `voice-led`/`root-position`,
  `range` as `C2-C5`/`C3-C6`/`C4-C7`, `bassOctaves` from 0 to 3, plus canonical
  `defaultLength` and `timeSignature` values).
- `valid` is false when parse or pitch-resolution diagnostics are present. Then
  `chords` is empty and `diagnostics` is populated with `offset`, `token`, and
  `message` entries. An empty
  but valid progression has `"valid": true` and `"chords": []`.
- `spelledTones` describes the chord itself. It does not include an added bass.
- `notes` gives the sounding notes for `voicing`. It includes an added bass. Custom
  chord note names include their octave.
- `voicing` is an array of integers identical in value to the human table's
  space-separated `MIDI` column. It includes an added bass.
- `bassMidi` and `bassNote` identify the added bass, or are `null` when bass is Off.
  `bassNote` includes its octave.
- `startBeat` is cumulative across chords.
- `durationBeats` is the resolved quarter-note duration.
- `lengthExpression` is the canonical written suffix or selected default.
  `usesDefaultLength` identifies inherited rows.
- `midiFile` is an absolute path string after a successful write, otherwise `null`.
- `error` is a string for a file I/O or empty-export failure, otherwise `null`.

Strings are escaped per JSON, and numbers are locale-independent. Whitespace and
indentation are not part of the contract. Consumers must parse the document. In JSON
mode, the tool writes no human result text to stdout. Usage errors remain
human-readable on stderr.

### Writing MIDI files

`--out <path>` (one-shot or piped mode) and `:export <path>` (REPL) write one loop
of the current progression as a Standard MIDI File. They use the same shared writer
as the plugin's export: the selected `--time-signature` (default 4/4), tempo from `--bpm` (default 120), track
name `Chord Text`, 960 ticks per quarter note, channel 1, and note-offs before
note-ons at equal ticks. `--out` requires a progression argument or piped input.
Using it to start an interactive REPL is a usage error — use `:export` there.

If the path does not already end in `.mid` (case-insensitive), the tool replaces its
final extension with `.mid`. A path with no extension gains one. Relative paths
resolve against the current working directory. The success line (and JSON `midiFile`)
report the absolute path. `:export` re-parses the last valid text with the current
settings, so the written file always matches what a report would show now. Writing to
an empty progression fails with exit code 2 in one-shot mode. The error goes to
stderr (or the JSON `error` field), and the tool prints no `Wrote` line.

The writer rejects a positive musical length if it cannot represent it safely as at
least one MIDI tick at 960 ticks per quarter note, or if the file timing is outside
the Standard MIDI File limits. This does not change parsing or live preview.

### Windows and UTF-8

The utility is Windows x64 only, consistent with the project. It builds with `wmain`
and converts command-line arguments to UTF-8, so chord tokens that contain `°` or `ø`
survive the command line. It sets the console input and output code pages to UTF-8,
so non-ASCII output displays correctly. It uses `std::filesystem::u8path` for
user-supplied paths. All numeric formatting and parsing uses the classic locale.

## Tests

`Tests/TheoryTest.cpp` is plain C++ with no JUCE dependency and covers parsing,
spelling, keys, voicing, and the render plan. `Tests/ProcessorTest.cpp` is a JUCE
console application covering transport behaviour, note lifecycle, parameters, state
round-trip, custom chord and bass output, the editor, Regenerate, card tooltips and
timing labels, the drag-file writer and its two modes, and MIDI export/read-back.
`Tests/ChordPreviewChecks.cpp` is called from
that suite and covers the preview exchange, processor arbitration and resynchronisation,
cancellation and lifecycle, independent instances, state/file invariance, card hit
testing, scaling, scrolled gestures, custom chord tooltips, displayed-revision
rejection, and editor cleanup. `Tests/CliTest.cpp` covers the command-line utility:
option parsing, custom chords, bass output, MIDI pitch parity, formatting, JSON, and
REPL state.

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

Recent runs printed `924 checks, 0 failures` for `TheoryTests`, `16796 checks, 0
failures` for `ProcessorTests`, and `366 checks, 0 failures` for `CliTests`. The
processor count varies because one preview stress check records how many coalesced
publications the reader observed. Each executable returns a non-zero exit code if any
check fails. The build scripts stop on the first failing suite. The build must
produce no warnings from project code under JUCE's recommended warning flags.

## Licensing

Chord Text links JUCE 8. JUCE 8 is available under the AGPLv3 or a commercial JUCE
license. Personal learning use without distribution is permitted under the AGPLv3.
Distribution of a build requires a deliberate licensing decision: comply with the
AGPLv3, or obtain a commercial JUCE license. This repository does not grant a
separate license for its own source.

## Known limitations and out-of-scope features

Known limitations in v1:

- Windows x64 only. There are no macOS or Linux builds.
- Live generation requires host PPQ and tempo. Without them, the plugin stays silent
  and does not free-run. Manual chord preview still works while the transport is
  stopped, but only while the host keeps processing the plugin.
- Chord preview is transient and per instance. The plugin cannot emit preview notes
  when a host suspends or disconnects it, and it cannot flush notes after
  `releaseResources`; the host owns that final flush.
- The plugin accepts MIDI input for host compatibility, but ignores it.
- Slash bass and applied chords (`V/V`) are unsupported on Roman numerals.
- Parenthesised alterations are unsupported. `Cb5` and `C#5` are read as root
  accidentals before the `5` quality. See
  [SYNTAX.md](SYNTAX.md#36-root-accidental-precedence-cb5).
- Ordinary upper chord tones sound once. A slash bass may duplicate its pitch class
  an octave below (for example, `C/E`). Custom lists can request octave differences,
  and the optional root bass can add another note. Identical final MIDI pitches
  sound once. There is no rootless voicing.
- The Degree column is best-effort for chromatic or ambiguous roots.
- `prepareToPlay` queues note-offs for pitches that were sounding. It emits them at
  sample 0 of the next block. `releaseResources` cannot emit MIDI, so it only clears
  internal note tracking. The host must flush sounding notes when it releases the
  plugin.
- The plugin reuses the host MIDI buffer by clearing it first. JUCE's VST3 wrapper
  normally preallocates enough capacity. A zero-capacity host buffer, however, may
  allocate on the first `MidiBuffer::addEvent`. Strict no-allocation depends on the
  host providing capacity.
- Export uses the last tempo observed in `processBlock`. If a project is exported
  before the host has processed a block since loading, the file uses 120 BPM.
- Bitwig may reject an external file drag from a VST3 plugin window. If Bitwig
  ignores the drop, the file is still saved, and **Reveal last clip** provides the
  fallback. Direct drop acceptance has not been verified in this environment.
- A drag clip has no tempo event, so Bitwig's project tempo is expected to govern
  playback. Bitwig may instead assume 120 BPM. This is host-dependent and unverified.
- Dragging writes a new file on every attempt under `Documents\ChordText\Clips`.
  Files accumulate, and there is no automatic cleanup in v1.
- There is no native Bitwig clip-creation API for a VST3 plugin. Chord Text writes
  a `.mid` file and starts an OS drag. It does not create or edit a Bitwig clip
  directly.
- The drag subtitle uses the current effective meter for its bar count. The drag
  file remains beat-based and deliberately has no tempo or time-signature events.
- The editor uses a 960×650 base layout with four UI scales from 75% to 150%. The
  DEFAULT LENGTH and BASS menus sit beside the scale summary. Actual host window
  resizing and mixed-DPI display behavior still require DAW checks.
- Host state restore is assumed to run on the message thread. If a host calls
  `setStateInformation` on another thread, the plugin defers the plan rebuild to the
  message queue. The host is still responsible for serialising state changes.
- Only one progression is active at a time. There are no saved presets beyond the
  host project state.

Command-line utility limitations:

- Windows x64 only, consistent with the project.
- No audio preview. The tool reports text and writes MIDI files only.
- The REPL is line-oriented with no history, editing, or completion.
- `--bpm` defaults to 120 because there is no host tempo to observe.
- JSON in the REPL is one document per report, interleaved with human command
  feedback on stderr. It is not a single JSON stream.
- The shared writer replaces the JUCE export implementation. The existing read-back
  tests guard parity, rather than a byte-for-byte comparison.

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
export, Regenerate, card timing labels, the drag-file writer, and the clip-preview
state. The following host checks require a VST3-compatible DAW and a human at the
keyboard. They are documented here, but they were **not** performed as part of this
repository's automated verification. They therefore remain unverified until someone
runs them.

- [ ] Route **Chord Text** to an instrument and hear the default progression.
- [ ] With the transport stopped, left-press a chord card and confirm the chord
      sounds; release and confirm it stops. Confirm this works with no playhead if
      the host offers such a mode.
- [ ] During playback, left-press a card and confirm it overrides the progression
      without stopping the timeline; release and confirm playback resumes at the
      current position rather than restarting.
- [ ] Right-click a card, use the DAW while it is held, and confirm ordinary focus
      loss does not release it. Right-click it again to stop.
- [ ] Release a momentary preview outside the plugin window and confirm no stuck
      notes remain.
- [ ] Press the sounding card again and confirm it retriggers without layering.
- [ ] Hold one card and left-press another; release the left button and confirm the
      held chord does not return.
- [ ] While a chord is held, change key, mode, scale mode, voicing, range, velocity,
      bass, and Loop one at a time and confirm each cancels the hold without restarting it.
- [ ] Start editing the progression text and confirm a held chord stops before the
      150 ms parse.
- [ ] Bypass, reset processing, save/reload the project, and close/reopen the editor
      while a chord is held; confirm the hold stops and does not restart.
- [ ] Change UI scale and export or drag a clip while a chord is held; confirm the
      preview continues.
- [ ] Zoom and scroll the chord sequence, then left-press and right-click a visible
      card at 75%, 100%, 125%, and 150%; confirm the intended card sounds and
      releases.
- [ ] Release a preview exactly at a chord boundary and confirm the next chord starts
      once, with no duplicate note-ons or restart.
- [ ] Change the text to `C, Cmaj7, Dmaj7, Emin9, Fmaj7` and confirm the notes.
- [ ] Change to `i-iv-V` in A minor and confirm Am, Dm, E.
- [ ] Test default and explicit lengths, for example `Am7:1/8D, Dm7:2beats, G7:1bar`.
- [ ] Change DEFAULT LENGTH during playback. Confirm inherited steps update while
      explicit note and beat lengths retain their duration.
- [ ] Change the host meter among 4/4, 3/4, 6/8, and 7/8 during playback. Confirm
      bar lengths, cards, loop phase, export metadata, and the drag subtitle update.
- [ ] Remove the host meter or use an unavailable transport. Confirm the editor shows
      the 4/4 fallback and bar lengths use four beats.
- [ ] Switch between voice-led and root-position voicing and confirm audibly
      different, valid voicings.
- [ ] Start, stop, seek, loop, and bypass during playback; confirm there are no
      stuck notes.
- [ ] Switch keys and confirm Roman numerals resolve against the selected key while
      absolute chord symbols stay unchanged.
- [ ] Enter `[1,2,4,5]` in C Major and confirm the card and MIDI output use C, D, F, G.
- [ ] Enter `[1,3,5,F]`, then change key. Confirm the named F stays F while the
      scale degrees follow the selected key.
- [ ] Enter `[F3,1,3,5]` and confirm the first note stays F3 when Range or Voicing changes.
- [ ] Set BASS to Off, 1, 2, and 3 octaves down. Compare live playback, card preview,
      held preview, MIDI export, and drag-clip pitches. Check that bass follows the
      chord root, including when that root is not the lowest chord note.
- [ ] Save and reload a project with BASS enabled. Confirm the setting is restored.
      Automate `bassOctaves` and confirm the current preview is cancelled.
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
- [ ] Confirm the chord cards show the expected degrees and spelled notes for
      the current key and voicing.
- [ ] Save and reload the project; confirm the text, key, mode, voicing, range,
      velocity, default length, loop, and bass settings restore. Load an older
      project and confirm its default length is four beats.
- [ ] Confirm cards show DEFAULT only for inherited steps and show the selected or
      written musical length with its resolved beat count.
- [ ] Change the text and press Regenerate; confirm the cards update immediately.
- [ ] Enter invalid text and press Regenerate; confirm the previous rows and plan
      stay and diagnostics appear.
- [ ] In Bitwig, drag the clip preview into the arranger or onto a track.
- [ ] Confirm a MIDI clip appears with the expected notes and loop length.
- [ ] Confirm the dragged clip follows the project tempo.
- [ ] If the drop is ignored, click Reveal last clip and drag the file from
      Explorer into Bitwig.
- [ ] Confirm Export `.mid` still works and that the exported file retains tempo
      and the current effective time-signature metadata. Confirm a drag clip omits
      both metadata events.

`pluginval` is optional. If it is installed, run it against the built VST3 and fix
anything that affects this plugin. It is not required to build or use Chord Text.

## Repository layout

```text
CMakeLists.txt              plugin, test executables, JUCE subdirectory
README.md                   this document
SYNTAX.md                   canonical chord and progression syntax guide
install-vst3.bat            copy the Release VST3 bundle to the system VST3 folder
build.sh / build.bat        configure/build/test wrappers
Source/Theory/              JUCE-free notes, keys, chords, parsers, voicing, render plan,
                            MIDI file writer
Source/Plugin/              processor (transport, state, export, drag files, preview), editor,
                            chord preview exchange, clip drag component
Source/Cli/                 command-line utility (options, report, session, main)
Tests/                      TheoryTests, ProcessorTests, and CliTests
docs/superpowers/specs/     design specification
libs/JUCE/                  pinned JUCE checkout (ignored by Git)
build/                      generated build tree (ignored by Git)
```
