# MIDI Chord Generator — Design Spec

Date: 2026-10-21
Status: Approved design, pending written-spec review

Working names: project `ChordText`, product `Chord Text`, CMake target `ChordText`.
These are placeholders; naming is settled before scaffolding.

## 1. Intent

Build a VST3 MIDI generator for Windows that turns a text description of a chord
progression into MIDI notes. The user types chord symbols or Roman numerals, and
the plugin either plays the progression in sync with the host transport or exports
it as a Standard MIDI File.

The project serves two purposes:

1. **Composition** — audition and record generated chord progressions in a DAW.
2. **Music-theory learning** — see each chord's degree and correctly spelled
   chord tones while hearing the result.

Success means the author can type a progression, hear it at the host tempo, record
it into the DAW, export the same progression as a `.mid` file, and explain how the
parser, key model, voicing, and render plan fit together.

### Success criteria

1. A clean checkout builds with the documented commands and produces a VST3.
2. `C, Cmaj7, Dmaj7, Emin9, Fmaj7` parses and displays the correct notes.
3. `I-IV-V` in C major parses as C, F, G; in A minor, `i-iv-V` parses as
   Am, Dm, E.
4. Live playback follows host tempo, defaults to one chord per 4/4 bar, and loops.
5. Explicit durations such as `Am7:2` change the chord length in beats.
6. Voice-led and root-position modes both produce valid, audibly different
   voicings.
7. Export writes a `.mid` whose notes and timing match live playback.
8. Parse errors identify the failing token and character position, and never
   replace the last valid playback plan.
9. Stop, seek, loop, bypass, and plan changes never leave stuck notes.
10. Saving and restoring the project restores text and all generator settings.
11. The editor shows chord, degree, and spelled notes, and highlights the current
    chord during playback.
12. The theory and plugin test suites pass from the command line.

### Explicitly out of scope for v1

Standalone application, live MIDI-input triggering, MIDI Thru, key changes or
modulation, secondary dominants, 11th and 13th chords, MPE, MIDI 2.0, drag-out
clips, custom UI styling, a built-in synth or audio output, and CI.

## 2. Environment and constraints

Verified environment:

- Windows 10/11, 64-bit.
- Visual Studio 2022 Build Tools with the C++ workload (MSVC 14.44 or compatible).
- CMake 3.22 or newer; the Build Tools copy may be used by full path.
- JUCE 8.0.15, cloned once into `libs/JUCE`, which is git-ignored.
- FL Studio 2025 for in-host verification.
- No IDE required; the build is command-line driven.

Constraints:

- C++17, JUCE, and the standard library only.
- Generator: Visual Studio 17 2022, platform x64; do not use Ninja.
- Platform target is Windows x64 only.
- `processBlock` must not allocate, lock, perform file I/O, or destroy objects.
- Chord parsing, theory, and voicing must be usable without JUCE.
- Parameter IDs are stable strings and must not change once saved projects exist.
- No build warnings from code we write, using JUCE's recommended warning flags.

### Licensing

JUCE 8 is AGPLv3 or commercial. Personal learning use without distribution is
permitted under AGPLv3. Distribution requires a deliberate licensing decision.

## 3. Technology choice

**JUCE 8 + CMake + MSVC, built as a VST3 MIDI effect.**

Rationale:

- JUCE supplies the VST3 wrapper, `MidiBuffer`, MIDI-file writing, transport
  access, and the editor widgets.
- A MIDI effect matches the product: it emits notes and produces no audio.
- The pure C++ core keeps parsing and theory testable in milliseconds.

Rejected alternatives:

- **Standalone app first** — lower host-routing risk, but it does not satisfy the
  requirement for live MIDI in the DAW.
- **Instrument with a built-in synth** — self-contained, but duplicates the DAW's
  instruments and does not solve MIDI routing to another track.
- **Web or scripting prototype** — useful for parser experiments, but adds a
  second stack and does not answer the plugin-routing question.

## 4. Architecture

### 4.1 Repository layout

```
vst-chord-generator/
  CMakeLists.txt                  build definition: plugin, theory tests, plugin tests
  README.md                       build, install, syntax reference, testing
  .gitignore                      build/, libs/, .vs/, *.vst3, .superpowers/
  build.sh / build.bat            configure/build/test wrappers
  Source/
    Theory/
      Note.h/.cpp                 spelled pitches, pitch classes, MIDI conversion
      Key.h/.cpp                  tonic, mode, scale degrees, spelling
      Chord.h/.cpp                root, quality, intervals, spelling, degree label
      ChordSymbolParser.h/.cpp    absolute chord-symbol grammar
      RomanNumeralParser.h/.cpp   functional grammar relative to a key
      Progression.h/.cpp          parsed steps, durations, diagnostics
      Voicer.h/.cpp               root-position and voice-led realization
      RenderPlan.h/.cpp           timed note events for one loop
      Diagnostic.h                structured parse/validation errors
    Plugin/
      PluginProcessor.h/.cpp      transport sync, live scheduling, state, export
      PluginEditor.h/.cpp         text entry, controls, learning table, diagnostics
  Tests/
    Check.h                       shared CHECK / CHECK_NEAR / reportAndExit
    TheoryTest.cpp                pure C++ theory and parser tests
    ProcessorTest.cpp             JUCE console tests for transport, state, export
  docs/superpowers/specs/         this document and later plans
```

### 4.2 Component boundaries

| Unit | Knows about | Must not know about |
|---|---|---|
| `Theory` | notes, keys, chords, parsing, voicing, beats | JUCE, hosts, audio devices, files |
| `PluginProcessor` | transport, `MidiBuffer`, MIDI files, state | chord parsing or voicing internals |
| `PluginEditor` | controls, text entry, display, diagnostics | DSP scheduling internals |

The theory library has no JUCE dependency. It consumes text and a key and produces
a `RenderPlan` of timed note events. The plugin adapts those events to the host
and to MIDI-file writing.

### 4.3 Data flow

```
text + key + voicing settings
        │
        ▼
  parser + theory model  ──►  Progression + diagnostics
        │
        ▼
       Voicer            ──►  concrete MIDI pitches per step
        │
        ▼
     RenderPlan          ──►  timed note-on / note-off events
        │
        ├── live adapter  ──►  MidiBuffer at host tempo
        └── export adapter ──►  Standard MIDI File
```

### 4.4 Plan publication

Parsing and voicing run on the message thread. A successful parse builds an
immutable `RenderPlan`. Plans are triple-buffered in the processor and published
to the audio thread through atomic `published` and `observed` indices.

- The audio thread reads the published slot and never allocates, locks, or
  destroys a plan.
- The message thread writes a slot that is neither published nor observed, so it
  never overwrites a plan the audio thread may be reading.
- An invalid parse leaves the previous plan active.
- A plan change, stop, seek, or bypass releases active notes before new events
  are scheduled.

## 5. Input language

### 5.1 Progression text

The key selector supplies tonic and mode. The text field contains the progression.

```
Cmaj7, Am7:2, Dm7, G7
i, IV, V
C | Am7 | F | G
```

- Chords are separated by commas, whitespace, newlines, or `|`.
- `|` is visual only; it does not enforce a bar boundary or change timing.
- Default chord duration is 4 beats.
- A suffix `:N` sets duration in beats: `Cmaj7:2`, `G7:1.5`.
- Durations must be positive finite numbers. Zero, negative, NaN, infinity, and
  malformed values are errors.
- Empty text is valid and produces an empty plan.
- Whitespace and newlines are ignored between tokens.
- Absolute and Roman-numeral tokens may be mixed in one progression.

The key selector always matters for Roman numerals and for the learning table's
degree column. It does not alter absolute chord symbols.

### 5.2 Absolute chord symbols

Grammar:

```
chord       := root quality? alteration* ("/" bass)?
root        := [A-G] ("#" | "b")?
bass        := [A-G] ("#" | "b")?
alteration  := ("b" | "#") ("5" | "9")
```

Supported qualities:

- Triads: major (empty), `m`, `min`, `dim`, `o`, `°`, `aug`, `+`, `sus2`,
  `sus4`, `5`.
- Sixths: `6`, `m6`, `min6`.
- Sevenths: `7`, `maj7`, `M7`, `m7`, `min7`, `mMaj7`, `minMaj7`, `dim7`,
  `o7`, `°7`, `m7b5`, `ø7`, `7sus4`.
- Ninths: `9`, `maj9`, `M9`, `m9`, `min9`, `add9`, `9sus4`.
- Alterations: `b5`, `#5`, `b9`, `#9`.
- Slash bass: `C/E`, `Am7/G`, `F#m7b5/C`.

Examples:

```
C  Cmaj7  Dmaj7  Emin9  Fmaj7
Dm7  G7  Cmaj7  A7b9
C/E  Am7/G  F#m7b5/C
```

Unsupported or ambiguous tokens are errors, not guesses. Examples include `C99`,
`C11`, `C13`, `C6/9`, and `H`.

Root spelling is preserved from the input. Chord tones are spelled by letter and
interval, so C# major is C#–E#–G# and G#maj7 contains F##. Display spelling is
independent of the MIDI pitch number.

### 5.3 Roman numerals

Grammar:

```
degree      := accidental? numeral quality? alteration*
accidental  := "b" | "#"
numeral     := "I" | "II" | "III" | "IV" | "V" | "VI" | "VII"
```

- Case determines default triad quality: uppercase = major, lowercase = minor.
- `°` or `o` = diminished; `+` = augmented.
- An explicit quality suffix overrides the case default.
- Extensions and alterations use the same suffix vocabulary as absolute chords:
  `V7`, `ii7`, `IVmaj7`, `viio7`, `bVII`, `#iv`, `V7b9`.
- The numeral selects a scale degree in the selected key.
- Accidentals alter the degree root chromatically.
- Slash bass and tokens such as `V/V` are not supported on Roman numerals in v1.

Examples in C major:

```
I       C
ii      Dm
V7      G7
IVmaj7  Fmaj7
bVII    Bb
#iv     F#m
```

Examples in A minor:

```
i       Am
iv      Dm
V       E
V7      E7
```

Minor keys use natural-minor spelling for the scale degrees. Chromatic notes come
from accidentals or from the chord quality itself. In A minor, `V` therefore
produces E–G#–B, and the G# is spelled correctly. `vii°` in A minor uses the
natural-minor G; use `#vii°` for a G# diminished chord.

### 5.4 Degrees in the learning table

The Degree column is a normalized functional label:

- Cmaj7 in C major → `Imaj7`
- Am7 in C major → `vi7`
- Dm7 in C major → `ii7`
- G7 in C major → `V7`
- Cm in C major → `i`
- Bb in C major → `bVII`

Absolute chords are mapped to the nearest scale degree by pitch class. If the root
is chromatic, the label uses `b` or `#`; for genuinely ambiguous spellings the
label is best-effort and the column may show `—`. Chromatic degree labels are not
a success criterion.

### 5.5 Diagnostics

Every error carries:

- the failing token,
- a zero-based character offset into the text,
- a short message.

Example:

```
"Emin99" at character 12: unknown extension "99"
```

If any token is invalid, the parse produces no new progression. The previous valid
plan continues playing, and the editor shows all diagnostics.

## 6. Harmony model

### 6.1 Keys

- Modes: major and minor only.
- Major scale intervals: 0, 2, 4, 5, 7, 9, 11.
- Natural-minor scale intervals: 0, 2, 3, 5, 7, 8, 10.
- Tonic spelling comes from the key selector (for example C, C#, Db, Eb, F#).
- Scale degrees are spelled by letter, with accidentals as required.
- Double accidentals are used when correct spelling requires them (for example
  the G# major scale contains F##, and G#maj7 contains F##).

### 6.2 Chord tones

A chord contains its root plus the intervals implied by its quality and
extensions:

- Triad: root, third, fifth.
- Sixth: root, third, fifth, sixth.
- Seventh: root, third, fifth, seventh.
- Ninth: root, third, fifth, seventh, ninth.
- `add9`: root, third, fifth, ninth, without a seventh.
- `sus2`/`sus4`: replace the third with the second or fourth.
- `7sus4`/`9sus4`: sus4 plus the minor seventh, and optionally the ninth.
- `5`: root and fifth only.
- Diminished and augmented qualities alter the third and fifth accordingly.
- `m7b5`: root, minor third, diminished fifth, minor seventh.
- Alterations replace or add the named chord tone: `b5`, `#5`, `b9`, `#9`.

All chord tones are sounded; v1 does not omit roots or use rootless voicings.

### 6.3 Voicing

Default range: C3–C6 (MIDI 48–84). The user may choose C2–C5, C3–C6, or C4–C7.

**Root position**

- The root is placed at the lowest octave that fits the range.
- Remaining chord tones are stacked above it in interval order.
- Slash bass is the lowest note; upper chord tones sit above it.

**Voice-led**

- The first chord is root position.
- Each later chord tries its inversions and octave placements within the range.
- The chosen voicing minimizes total absolute semitone movement from the previous
  chord. Both pitch lists are sorted ascending, and the lowest `min(n, m)` notes
  are paired one-for-one; unmatched upper notes are ignored.
- Ties prefer the smaller maximum leap, then the lower average pitch.
- Slash bass remains fixed as the lowest note; upper voices are chosen above it.
- Each chord tone appears once; no octave doubling in v1.

If a chord cannot fit the range after octave adjustment, it is transposed to the
nearest valid octave and clamped; this case is covered by tests.

### 6.4 Velocity and note lengths

- Default velocity is 100; range 1–127.
- A chord's note-on occurs at its start beat.
- Its note-off occurs at its start beat plus duration.
- At a shared boundary, note-offs are scheduled before note-ons.
- Repeated identical pitches are released before the next attack.
- MIDI channel is 1.

## 7. Playback and transport

### 7.1 Plugin shape

- VST3 MIDI effect: `IS_MIDI_EFFECT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`,
  `NEEDS_MIDI_INPUT TRUE`, `IS_SYNTH FALSE`.
- No audio buses.
- The MIDI input port exists for host compatibility; incoming events are ignored
  in v1.
- The plugin does not call `setLatencySamples`; MIDI timing is expressed through
  sample offsets in the output buffer.

### 7.2 Transport sync

- Live generation requires `AudioPlayHead::getPosition()` with a PPQ position and
  tempo.
- Beat range for a block:
  `blockBeats = numSamples * bpm / (60 * sampleRate)`.
- Event sample offset:
  `round((eventBeat - blockStartBeat) * sampleRate * 60 / bpm)`, clamped to the
  block.
- Events at exactly the block end are scheduled in the next block.
- The progression loops with length equal to the sum of chord durations.
- The progression is anchored to song position 0, not to the moment playback
  starts. Starting at bar 5 begins at the corresponding phase of the loop.
- The loop phase is a non-negative `fmod(ppqPosition, loopLengthBeats)`, so
  negative count-in positions remain valid.
- Default loop is on; a loop toggle is provided.
- Tempo changes are picked up from each block's playhead data.
- If no usable playhead position is available, live generation is disabled and the
  editor reports that transport sync is unavailable. The plugin does not
  free-run.

### 7.3 Start, stop, seek, and loop

- Starting at beat 0 triggers the first chord at sample 0.
- Starting or seeking into the middle of a chord triggers that chord at the start
  of the block, with its note-off still at the chord boundary.
- A seek is detected when the current PPQ differs from the expected continuation
  by more than 0.25 beats.
- On stop, seek, bypass, prepare, or plan change, all generated notes are released
  before new events are emitted.
- On loop wrap, notes at the end are released before notes at the start.
- If loop is off, the progression plays once from its anchored start at song
  position 0; starting after its end produces no notes until the transport seeks
  back.

### 7.4 Plan updates

- Text parsing is debounced on the message thread.
- A valid parse builds a complete `RenderPlan` for one loop.
- The new plan is published atomically; the audio thread never blocks.
- Active notes from the old plan are released at the next block boundary.
- The new plan resumes at the current transport phase.
- Invalid text never replaces the active plan.

## 8. MIDI file export

- **Export .mid** writes one complete loop.
- Tempo is the current host tempo, or 120 BPM when no playhead tempo is available.
- The file includes a tempo event, 4/4 time signature, track name, and note events
  at 960 PPQ.
- Event ordering is deterministic; note-offs precede note-ons at the same tick.
- Export uses the same `RenderPlan` as live playback.
- Export is disabled while the current text has errors.
- The file chooser runs on the message thread.
- The written file must be readable by `juce::MidiFile` and by FL Studio.

## 9. Editor

Plain JUCE components, fixed initial size, keyboard focus enabled so typing works
inside the host.

Controls:

- Multi-line progression text editor.
- Key tonic selector.
- Major/minor selector.
- Voicing selector: Voice-led / Root position.
- Range selector: C2–C5, C3–C6, C4–C7.
- Velocity control, default 100.
- Loop toggle, default on.
- Export .mid button.
- Status line for transport state, parse state, and export result.

Learning table:

| Chord | Degree | Notes |
|---|---|---|
| Cmaj7 | Imaj7 | C E G B |
| Am7 | vi7 | A C E G |
| Dm7 | ii7 | D F A C |
| G7 | V7 | G B D F |

- The table uses the last valid progression.
- The current chord is highlighted during playback.
- Diagnostics appear below the table with token and character position.
- The editor does not include a piano-roll preview or drag-out clip in v1.

## 10. Saved state

The plugin saves:

- progression text,
- key tonic,
- major/minor mode,
- voicing mode,
- range,
- velocity,
- loop setting.

Numeric and enum settings use stable APVTS parameter IDs. The text is stored
alongside the APVTS state and re-parsed on load. Restoring state rebuilds the plan
but does not start playback.

Parameter IDs: `key`, `mode`, `voicing`, `range`, `velocity`, `loop`.
The text property key is `progressionText`.

## 11. Error handling and edge cases

- Invalid token: diagnostic with character offset; keep last valid plan.
- Empty input: empty plan, no notes, no errors.
- Duration `<= 0`, NaN, or infinity: error.
- Chord tone outside MIDI 0–127: transpose by octaves; if still impossible, clamp
  and report.
- Chord requiring double accidentals: display correctly; MIDI uses pitch class.
- Missing or unusable playhead: disable live generation; export still works at
  120 BPM.
- Transport stop, seek, bypass, prepare, and plan swap: release all active notes.
- Host calls `prepareToPlay` again: rebuild scheduling state and release notes.
- No audio output: the plugin never writes audio and never reports latency.
- Export with an empty progression: write an empty but valid MIDI file, or disable
  export. The chosen behavior is to disable export when there are no notes.
- Extreme text length: parser must terminate in bounded time; no unbounded
  backtracking.

## 12. Testing

### 12.1 Theory tests — plain C++, no JUCE

Parser:

- Every supported absolute quality and alias.
- Every supported alteration and slash bass.
- Commas, whitespace, newlines, and `|`.
- Durations: default, explicit, fractional, zero, negative, malformed.
- Invalid roots, unknown qualities, `C99`, `C11`, `C13`, `C6/9`, `H`.
- Roman numerals in major and minor keys, case-based quality, accidentals,
  explicit quality overrides, and invalid degrees.
- Mixed absolute and Roman numerals.
- Diagnostic offsets and messages.

Spelling and harmony:

- C# major = C#–E#–G#.
- G#maj7 contains F##.
- Enharmonic roots preserve input spelling.
- A minor `V` = E–G#–B.
- `Cmaj9`, `C6`, `Cadd9`, `Cm7b5`, `C7sus4`, `C9sus4`.
- MIDI pitch conversion for every chord tone.

Voicing and render plan:

- Root-position stacking and range clamping.
- Slash bass is always lowest.
- Voice-led movement is no larger than root-position movement for the same pair.
- All notes are within 0–127.
- Deterministic results for identical input.
- One chord per bar by default; explicit durations override.
- Note-off pairing, boundary ordering, loop length, empty plan.
- Long progressions and repeated identical pitches.

### 12.2 Plugin tests — JUCE console app

- Transport starts at beat 0 and emits the first chord.
- Transport starts mid-chord and emits the active chord.
- Stop releases all notes.
- Seek releases old notes and retriggers the active chord.
- Loop wraps cleanly with no stuck notes.
- Tempo change reschedules correctly.
- Missing playhead produces no notes.
- Invalid text keeps the old plan; valid text replaces it.
- State round-trip restores text, key, voicing, range, velocity, and loop.
- Export writes a readable `.mid`; read it back and verify tempo, ticks, note
  pairing, and note count.
- Export is disabled for invalid or empty progressions.

### 12.3 Manual verification

1. Build the walking skeleton and route it to an instrument in FL Studio.
2. Confirm live playback follows transport and tempo.
3. Record generated notes into the piano roll and check timing.
4. Export a `.mid`, import it, and compare with live playback.
5. Start, stop, seek, loop, bypass, and change text while playing; confirm no
   stuck notes.
6. Switch keys, voicing modes, ranges, velocity, and loop; confirm expected
   behavior.
7. Enter invalid text and confirm the last valid progression keeps playing with
   clear diagnostics.
8. Confirm the learning table shows correct degrees and spellings.

### 12.4 Build verification

A clean checkout plus the documented JUCE clone, configure, and build commands
produces the VST3 and both test executables. `build.sh --test` and
`build.bat test` run both suites and return non-zero on failure.

## 13. Milestones

1. **Walking skeleton** — MIDI-effect plugin, minimal `C`/`I` parsing, one chord
   per bar, routed to an instrument in FL Studio. This settles the highest-risk
   host-routing question.
2. **Theory core** — keys, notes, chords, absolute and Roman parsers, diagnostics,
   full theory tests.
3. **Voicing and render plan** — root-position and voice-led realization, timed
   events, tests.
4. **Live integration** — transport sync, note lifecycle, double-buffered plan
   publication, state save/load.
5. **Editor** — text entry, controls, learning table, diagnostics, current-chord
   highlight.
6. **Export** — deterministic `.mid` writer and round-trip tests.
7. **Polish** — README, syntax reference, manual verification, optional
   `pluginval`.

## 14. Risks

- **FL Studio MIDI routing** is the main risk. Milestone 1 exists to resolve it
  before the rest is built. If a pure MIDI effect cannot drive another instrument,
  the fallback is an instrument/audio-effect shape with MIDI output; the theory
  core and render plan do not change.
- **Host playhead support** varies. Live mode is disabled rather than free-running
  when PPQ or tempo is unavailable.
- **Parser ambiguity** is controlled by a small documented grammar and by
  rejecting unknown tokens.
- **Voice-leading quality** can be tested for invariants, but not for taste;
  manual listening is required.
- **Editor keyboard focus** must be configured correctly for typing in the host.
- **MIDI-file import** may interpret tempo or ticks differently in some hosts;
  verify in FL Studio and with a read-back test.

## 15. Verification summary

| Requirement | Verified by |
|---|---|
| Absolute and Roman parsing | Theory tests, success criteria 2–3 |
| Default and explicit timing | Theory tests, success criterion 5 |
| Voice-led and root-position | Theory tests, manual listening |
| Live transport and loop | Plugin tests, manual FL Studio check |
| No stuck notes | Plugin tests, manual start/stop/seek |
| MIDI export | Plugin tests, read-back, FL Studio import |
| Learning table and diagnostics | Editor review, manual check |
| State restore | Plugin state round-trip test |
| Build from clean checkout | Build verification, README |

## 16. Open questions

None. The plugin/product name and four-character plugin code are placeholders to be
settled during scaffolding; they do not affect the architecture.
