# Numeric scale degrees and nine scale choices

Status: approved by the user on 2026-10-23. Implemented and independently reviewed. Automated verification passed. Native DAW checks remain unverified.

## Intent

Let users enter familiar numbered progressions quickly, transpose them with Key,
and learn how chords belong to a selected scale. Build on the existing C++ theory
library, JUCE editor, processor, MIDI generation, and CLI.

## Decisions confirmed by the user

- Numbers 1 through 7 resolve to chords built from the selected scale.
- Bare numbers produce diatonic triads, including diminished and augmented triads.
- Minor uses natural minor: degree 5 in A Minor is Em.
- `2(7)` requests a diatonic seventh. Explicit quality overrides such as `2maj`,
  `2maj7`, and `2m7` are supported.
- Numeric, Roman, and absolute notation can be mixed in one progression.
- Existing separators, four-beat defaults, and `:duration` suffixes apply.
- Joined numbers such as `1645` are rejected.
- Chord cards show the resolved chord, numeric degree, and Roman analysis.
- A compact summary shows the tonic, scale name, and seven spelled scale notes.
- Nine scale choices: the seven major-scale modes, Harmonic Minor, and Melodic
  Minor. Melodic Minor always uses its ascending form.
- Roman roots follow the selected scale; numeral case and explicit suffixes keep
  their existing quality rules. Absolute chord pitches remain as written.
- New scale-derived extensions stop at triads and sevenths.
- Numeric root accidentals such as `b7` and `#4` are outside this feature.

## Scale definitions

Offsets are semitones above the selected tonic. Preserve diatonic letter spelling,
including double accidentals when required by a selected tonic.

| Display name | Offsets | CLI canonical name |
|---|---|---|
| Major (Ionian) | 0 2 4 5 7 9 11 | major |
| Minor (Aeolian) | 0 2 3 5 7 8 10 | minor |
| Dorian | 0 2 3 5 7 9 10 | dorian |
| Phrygian | 0 1 3 5 7 8 10 | phrygian |
| Lydian | 0 2 4 6 7 9 11 | lydian |
| Mixolydian | 0 2 4 5 7 9 10 | mixolydian |
| Locrian | 0 1 3 5 6 8 10 | locrian |
| Harmonic Minor | 0 2 3 5 7 8 11 | harmonic-minor |
| Melodic Minor | 0 2 3 5 7 9 11 | melodic-minor |

Key is always the tonic. C Dorian means C D Eb F G A Bb; selecting Dorian does
not change C to the tonic of a relative scale. The order above preserves the two
familiar choices first; this ordering is a proposed implementation choice.

CLI accepts case-insensitive canonical names plus `ionian` and `aeolian` aliases.
Existing `major` and `minor` output strings remain stable. CLI help, REPL `:mode`,
human reports, JSON settings, README, and SYNTAX must reflect the new modes.

## Numeric grammar and resolution

Each chord token starts with exactly one digit 1-7. No spaces occur inside a token.
Parse a duration suffix using the current progression tokenizer before resolving
the chord body. Preserve the original token and source offset for diagnostics.

| Form | Meaning | C Major example |
|---|---|---|
| `degree` | Stack scale degrees 1, 3, 5 from that root | `2` -> Dm |
| `degree(7)` | Stack scale degrees 1, 3, 5, 7 from that root | `2(7)` -> Dm7 |
| `degree quality` (without a space) | Use the scale root with explicit quality | `2maj7` -> Dmaj7 |
| Any accepted form followed by `:N` | Duration in beats | `2(7):8` -> Dm7 for 8 beats |

Wrap scale indexing across the tonic and account for octave offsets when finding
intervals. Recognize the resulting chord quality from its thirds, fifths, and
sevenths. Build ordinary ChordSpec objects so playback, voicing, export, and drag
share exactly the same notes and spelling.

Explicit overrides choose a chord quality independently of scale membership. The
scale determines only the root. Proposed finite override vocabulary:

- Triads: `maj`, `m`/`min`, `dim`/`o`/`°`, `aug`/`+`.
- Sevenths: `maj7`, `m7`/`min7`, `dom7`, `mMaj7`/`minMaj7`, `dim7`/`o7`/`°7`,
  `m7b5`/`ø7`, and `augMaj7`.
- `dom7` explicitly requests a dominant seventh. It avoids interpreting `27` as
  degree 2 plus a bare extension. All adjacent bare digit strings, including
  `17`, `27`, `57`, `01`, and `1645`, are rejected.
- Parenthesized `(7)` is used only after a bare degree. Do not accept combinations
  such as `2maj(7)` or `2(7)m`.
- No numeric slash bass, numeric root accidentals, numeric chord alterations, or
  numeric ninths in this release. `m7b5` is an explicit quality, not a general
  alteration syntax. Existing absolute and Roman syntax remains available.

The finite override list and `dom7` spelling are proposed builder choices for user
review. They complete the requested triad/seventh behavior without ambiguous digit
suffixes. Existing supported ninths on absolute and Roman chords remain supported.

Reject 0, 8, 9, malformed parentheses, unknown suffixes, numeric root accidentals,
and missing separators with specific diagnostics at the original token offset.
Examples should distinguish a degree-range error from unsupported syntax and
explain using separators for `1645`. Preserve existing duration errors and
all-or-nothing parsing, last-valid-preview, and action availability behavior.

## Representative musical results

| Scale | Input | Result |
|---|---|---|
| C Major | `1, 2, 3, 4, 5, 6, 7` | C, Dm, Em, F, G, Am, Bdim |
| A Minor | `1, 2, 3, 4, 5, 6, 7` | Am, Bdim, C, Dm, Em, F, G |
| C Major | `1(7), 2(7), 5(7), 7(7)` | Cmaj7, Dm7, G7, Bm7b5 |
| A Harmonic Minor | `1(7)` | AmMaj7: A C E G# |
| A Harmonic Minor | `3(7)` | Cmaj7#5: C E G# B |
| A Melodic Minor | `6(7)` | F#m7b5: F# A C E |
| C Dorian | `3, III, iii` | Eb, Eb, Ebm |
| A Minor | `5, 5maj, 5dom7` | Em, E, E7 |

Proposed representation: use the existing major-seventh quality plus a raised
fifth alteration for the augmented major seventh. Canonical output is `maj7#5`,
already expressible in the existing chord system; numeric `augMaj7` maps to it.
Do not add a general arbitrary-interval chord model just for this feature.

## Editor behavior

Preserve Chordscript's compact carbon/teal layout, action heights, Loop alignment,
validation states, and playback highlight. The Mode dropdown exposes nine choices
and provides enough width or fitted text for the longest name. Add a single compact
scale-summary line near the settings/sequence boundary without increasing the
editor size or obscuring inline errors.

For numeric `2(7)` in C Major, a card shows:

    Dm7
    Degree 2 · ii7
    D F A C
    4 BEATS

The notes retain current voiced order, so inversion can change their displayed
order. The numeric degree is explicit for numeric input; existing notation keeps
its current analysis and need not gain redundant source labels. The progression
editor retains the user's exact text. Empty-input help mentions numbers.

The scale summary reflects current settings even when text is invalid. If a
setting change cannot rebuild because input is invalid, retained cards remain
dimmed with their prior analysis and a clear Last valid preview label; never
relabel old sounding notes as if they belonged to the new valid plan.

## Architecture and compatibility

- Centralize scale definitions/names in the JUCE-free theory layer. Extend Key's
  scale-degree calculations and use the same definitions for summary notes,
  Roman roots, numeric chord resolution, and CLI parsing.
- Introduce a dedicated numeric-degree parser and add its dispatch to
  parseProgression. Do not preprocess numeric text into Roman text: that loses
  inferred quality semantics and original error offsets.
- Extend presentation data with the numeric source degree where applicable;
  keep canonical symbols and Roman analysis available to all consumers.
- Reuse the current render/voicing/MIDI paths. Numeric input must behave exactly
  like equivalent explicit chord input after resolution.
- Preserve the existing `mode` parameter ID, its two choices, and normalized
  automation mapping. Add the nine-choice parameter under a new ID (`scaleMode`)
  at the end of the parameter list; bind the visible Mode selector to it.
- Migrate legacy saved state without `scaleMode` from its stored Major/Minor
  selection. New state uses `scaleMode` as authoritative. Preserve existing text,
  parameter IDs, defaults, and old Major/Minor selections.
- Legacy `mode` parameter changes continue to select Major or Minor through the
  normal deferred rebuild path. UI changes to Major/Minor synchronize the legacy
  value with a recursion guard; extended choices leave the legacy value intact.
  Synchronization must not overwrite an extended scale during restore or echo
  an internal synchronization as a new legacy user event. New `scaleMode` is the
  parameter to automate for the full set of choices. Competing external events
  resolve in observed arrival order; restoring new state gives `scaleMode`
  precedence. This mechanism is a proposed compatibility choice.

## Verification and acceptance

- Test all seven triads and sevenths in all nine scales against independently
  stated expected tones, including diminished, augmented, minor-major, and
  augmented-major sevenths. Check representative sharp/flat spelling and octave
  wrap cases.
- Test mixed notation, overrides, unchanged Roman case semantics, durations,
  separators, and rejection diagnostics with source offsets.
- Check numeric and equivalent explicit progressions generate equivalent MIDI.
- Test nine-choice selection, summary text, card degree labels, invalid-state
  retention, and old/new state restoration and parameter synchronization.
- Exercise CLI options and REPL modes, JSON canonical settings, and shared parsing.
- Run the project build and theory/processor/CLI suites. Report native DAW checks
  separately when a host is available; automated tests alone do not prove host
  automation compatibility or external drag acceptance.

## Scope boundaries and builder discretion

No other harmonic/melodic minor modes, new scale-derived ninths, new voicing
algorithms, rhythm features, or numeric bass/accidental syntax. Do not publish a PR
or implement product code as part of this design step.

The builder may settle file organization, internal helpers, exact concise error
wording, test organization, and typography within the existing compact design.
User approval of this complete description precedes implementation planning.
