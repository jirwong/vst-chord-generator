# Chord Text — Syntax Guide

Chordscript accepts chord symbols, Roman numerals, and numeric scale degrees. The plugin editor and the
`chordtext-cli` utility use the same parser, so this guide applies to both.
The plugin produces MIDI only. The syntax determines which notes are generated.

## At a glance

```text
Cmaj7, Am7, Dm7, G7       absolute chords
i-iv-V                    Roman numerals
Cmaj7:1/8D, Am7:2beats    musical lengths
G7:1bar                    bar length from the current meter
C/E, F#m7b5/C             slash bass
1, 6, 4, 5                numeric scale triads
1(7), 2(7), 5(7):1/8T     numeric scale sevenths and musical lengths
[1,2,4,5]                 custom notes: C D F G in C Major
[1,3,5,F]                 degrees and a named note in one custom chord
```

- Every chord uses the selected default length when it has no suffix. New
  projects start with **4 beats**.
- Chords can be separated by commas, spaces, tabs, newlines, `|`, or `-`.
- Empty input is valid and produces an empty progression.
- Absolute chords and Roman numerals can be mixed in one progression.

## 1. Writing a progression

A progression is a sequence of chord tokens. These all describe the same
four-chord progression:

```text
C, Am7, F, G
C Am7 F G
C | Am7 | F | G
C-Am7-F-G
```

```text
C
Am7
F
G
```

Rules:

- Comma, space, tab, newline, `|`, and `-` are separators.
- Separators may be mixed.
- Repeated separators and leading or trailing separators are ignored.
- `|` and `-` are **visual only**. They do not force a bar boundary and do not
  change timing. Lengths control timing.
- Empty text, or text containing only separators, is valid. It produces an
  empty progression with no notes and no errors.
- Absolute chords, Roman numerals, and numeric degrees may be mixed in one progression.

> A hyphen is **not** a minor-quality alias. `C-E-G` means three chords
> (C, E, G), not C minor. Write `Cm` or `Cmin` for C minor.

## 2. Musical lengths

Add `:length` to any chord token. The suffix may be a note value, a beat
amount, or a bar amount:

```text
Cmaj7:1/8D, Am7:2beats, Dm7:1bar, G7:1/8T
[1,3,5,F]:3/16, 6:1.5beats, 5:2bars
```

One internal beat is always one quarter note, including in 6/8. Existing bare
numbers retain their meaning as quarter-note beats: `C:2` is two beats in every
meter. The selected **DEFAULT LENGTH** applies only when the token has no suffix.

| Input | Quarter-note beats |
|---|---:|
| `1/2`, `1/2D`, `1/2T` | 2, 3, 4/3 |
| `1/4`, `1/4D`, `1/4T` | 1, 3/2, 2/3 |
| `1/8`, `1/8D`, `1/8T` | 1/2, 3/4, 1/3 |
| `1/16`, `1/16D`, `1/16T` | 1/4, 3/8, 1/6 |
| `3/16` | 3/4 |
| `N` or `Nbeat` / `Nbeats` | N beats |
| `Nbar` / `Nbars` | N × the current bar length |

Note fractions accept a positive integer numerator and a denominator of 1, 2,
4, 8, 16, 32, or 64. `D` means dotted and multiplies by 3/2. `T` means
triplet and multiplies by 2/3. Lower-case `d` and `t` are accepted and shown
as uppercase `D`/`T`: `1/8d` displays as `1/8D`. Beat and bar amounts accept
positive finite decimals. Unit words are case-insensitive. Spaces are not
allowed inside a suffix. Beat and bar numbers use the legacy numeric parser,
which also accepts exponent notation such as `1e2` (100 beats).

The plugin's menu has 19 stable choices: half, quarter, eighth, and sixteenth
notes with straight, dotted, and triplet variants; `3/16`; 1, 2, and 4 beats;
and 1, 2, and 4 bars. Four beats and one bar remain distinct choices and labels
even when the current meter makes them equal. The CLI also accepts valid entered
forms such as `5/16` that are not menu choices.

Bar lengths use the DAW's current time signature. One bar is four beats in
4/4, three beats in 3/4 or 6/8, and 3.5 beats in 7/8. If the DAW does not
supply a valid signature, the plugin uses 4/4 and marks it as a fallback. The
CLI accepts signatures with a numerator from 1 to 255 and a denominator of 1,
2, 4, 8, 16, 32, or 64; other values are errors. A host meter change
re-resolves every bar-based step, including inherited steps whose default is a
bar amount. Note and beat lengths do not change. Changing
DEFAULT LENGTH during playback affects only inherited steps.

Lengths affect live playback, MIDI export, drag clips, the loop length, and
the chord cards. A card shows the written or selected length, its resolved
beat count, and **DEFAULT** only when it inherits the selected default. The
loop length is the sum of resolved step lengths. A chord's note-on occurs at
its start beat and its note-off occurs at start + length.

Invalid examples:

```text
Cmaj7:0        error: invalid length "0"
Cmaj7:1/3      error: invalid length "1/3"
Cmaj7:1/8DT    error: invalid length "1/8DT"
Cmaj7:1beatD   error: invalid length "1beatD"
Cmaj7: 2       error: invalid length ""
Cmaj7:nan      error: invalid length "nan"
```

## 3. Absolute chords

### 3.1 Grammar

```text
chord      := root quality? alteration* ("/" bass)?
root       := [A-G] ("#" | "b")?
bass       := [A-G] ("#" | "b")?
alteration := ("b" | "#") ("5" | "9")
```

Root and bass letters are uppercase `A` through `G`. A root may have one
accidental, `#` or `b`.

### 3.2 Root spelling

The spelling from the input is preserved:

```text
C#       -> C# E# G#
G#maj7   -> G# B# D# F##
Cb       -> Cb Eb Gb
```

Octave and MIDI pitch are chosen by the voicer. The chord cards use each
chord's spelled tones.

### 3.3 Supported qualities and aliases

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
Quality matching is case-sensitive: `M7` is a major seventh, `m7` is a minor
seventh.

Examples:

```text
C        C major triad
Cm       C minor triad
Cdim     C diminished triad
C°       C diminished triad
Caug     C augmented triad
C+       C augmented triad
Csus4    C suspended fourth
C5       C power chord
C6       C major sixth
Cm6      C minor sixth
Cmaj7    C major seventh
CM7      C major seventh
Cm7      C minor seventh
CmMaj7   C minor-major seventh
Cdim7    C diminished seventh
Cm7b5    C half-diminished seventh
C7sus4   C dominant seventh suspended fourth
C9       C dominant ninth
Cmaj9    C major ninth
Cm9      C minor ninth
Cadd9    C major triad with an added ninth
```

### 3.4 Alterations

`b5`, `#5`, `b9`, and `#9` may follow the quality, in any order and in any
combination:

```text
C7b9
C7#9
Cm7b5
C7#5b9
```

- `b5` / `#5` alter the fifth.
- `b9` / `#9` alter the ninth.
- If the base quality does not contain that degree, the altered tone is added.
- `add9` adds the ninth without a seventh.

### 3.5 Slash bass

Add `/` and a bass note to fix the lowest note:

```text
C/E
Am7/G
F#m7b5/C
```

- The slash bass is always the lowest note.
- The remaining chord tones are voiced above it.
- Slash bass is supported on absolute chords only, not on Roman numerals.
- The slash bass spelling is preserved.

### 3.6 Root accidental precedence: `Cb5`

The parser consumes a root accidental immediately after the root letter,
before reading the quality. Therefore:

| Written | Read as |
|---|---|
| `Cb5` | C-flat power chord (Cb plus its fifth) |
| `C#5` | C-sharp power chord |
| `C7b5` | C dominant seventh with a flat fifth |
| `Cm7b5` | C half-diminished seventh |

Write alterations only after an explicit quality, for example `C7b5`,
`Cm7b5`, or `C7#5`. Parenthesised forms such as `C(b5)` are not supported.

### 3.7 Unsupported absolute tokens

Unknown or ambiguous tokens are errors, not guesses. Examples:

```text
C99
C11
C13
C6/9
H
C##
C(b5)
c
```

`c` is invalid because root letters must be uppercase. Double accidentals such
as `C##` are not supported. `H` is not a note name in this syntax.

## 4. Roman numerals

### 4.1 Grammar

```text
degree     := accidental* numeral quality? alteration*
accidental := "b" | "#"
numeral    := "I" | "II" | "III" | "IV" | "V" | "VI" | "VII"
```

### 4.2 Case and quality

- Uppercase numerals are major by default: `I`, `IV`, `V`.
- Lowercase numerals are minor by default: `i`, `ii`, `vi`.
- `°` or `o` makes the triad diminished: `vii°`, `viio`.
- `+` makes the triad augmented.
- An explicit quality suffix overrides the case default, using the same
  vocabulary as absolute chords.
- Bare extensions follow the case:
  - lowercase `ii7` is a minor seventh;
  - uppercase `V7` is a dominant seventh;
  - `IVmaj7` is a major seventh.

The case of the numeral's first letter determines the default quality. Use all
uppercase or all lowercase for clarity.

Examples in C major:

```text
I        -> C
ii       -> Dm
V7       -> G7
IVmaj7   -> Fmaj7
bVII     -> Bb
#iv      -> F#m
```

Examples in A minor:

```text
i        -> Am
iv       -> Dm
V        -> E
V7       -> E7
vii°     -> Gdim
#vii°    -> G#dim
```

### 4.3 Accidentals before the numeral

`b` and `#` before the numeral alter the scale-degree root chromatically:

```text
bVII     -> Bb in C major
#iv      -> F#m in C major
#vii°    -> G#dim in A minor
```

One accidental is typical. The parser also accepts repeated accidentals.

### 4.4 Key and mode

The key selector supplies the tonic spelling. The Mode selector supplies the
selected scale.

- The key always matters for Roman numerals.
- Minor (Aeolian) uses natural-minor spelling for scale degrees.
- Other modes use the selected scale's degree roots. Roman case and quality rules
  do not change. In C Dorian, `III` is Eb major and `iii` is Eb minor.
- Chromatic notes come from accidentals or from the chord quality itself.
- In A minor, `V` is E–G#–B, with the G# spelled correctly. `vii°` uses the
  natural-minor G.

Selectable tonics are:

```text
C  C#  Db  D  D#  Eb  E  F  F#  Gb  G  G#  Ab  A  A#  Bb  B
```

in any of the nine supported scales.

The key selector does **not** transpose or alter absolute chord symbols. `C` is
C major regardless of the selected key.

### 4.5 Unsupported Roman forms

- Slash bass is not supported on Roman numerals.
- Applied or secondary numerals such as `V/V` are not supported in v1.

## 5. Keys, modes, and what affects parsing

| Setting | Affects syntax? | Notes |
|---|---|---|
| Key | Yes, for Roman numerals and numeric degrees | Sets the tonic; absolute chord pitches are unchanged |
| Mode | Yes, for Roman numerals and numeric degrees | Sets scale roots and numeric chord qualities |
| Voicing | No | Changes the generated pitches, not the parse |
| Range | No | Changes the generated octaves, not the parse |
| Velocity | No | Changes MIDI velocity, not the parse |
| Default Length | Yes, for tokens without a suffix | Per-project default; starts at 4 beats |
| Loop | No | Changes playback repetition, not the parse |
| Bass | No | Adds the voiced root at a lower octave; it does not change the input |

The current DAW time signature is runtime context, not a saved selector. It
resolves only bar expressions. Missing or invalid host meter uses the 4/4
fallback.

### 5.1 Supported scales

Key always sets the tonic. For example, Key C with Mode Dorian uses C Dorian.
The scale summary shows the tonic, scale name, and seven notes.

| Mode | Notes with tonic C | CLI name |
|---|---|---|
| Major (Ionian) | C D E F G A B | `major` |
| Minor (Aeolian) | C D Eb F G Ab Bb | `minor` |
| Dorian | C D Eb F G A Bb | `dorian` |
| Phrygian | C Db Eb F G Ab Bb | `phrygian` |
| Lydian | C D E F# G A B | `lydian` |
| Mixolydian | C D E F G A Bb | `mixolydian` |
| Locrian | C Db Eb F Gb Ab Bb | `locrian` |
| Harmonic Minor | C D Eb F G Ab B | `harmonic-minor` |
| Melodic Minor | C D Eb F G A B | `melodic-minor` |

Melodic Minor always uses the ascending form. It does not change with melodic
direction. CLI mode names are case-insensitive. `ionian` and `aeolian` are aliases
for `major` and `minor`.

### 5.2 Numeric scale chords

Enter one degree from `1` to `7`. The plugin builds a triad from that scale.
Add `(7)` to build a scale seventh. Each chord uses alternate scale notes from
its root. The scale determines the chord quality.

| Scale | Input | Result |
|---|---|---|
| C Major | `1, 2, 3, 4, 5, 6, 7` | C, Dm, Em, F, G, Am, Bdim |
| A Minor | `1, 2, 3, 4, 5, 6, 7` | Am, Bdim, C, Dm, Em, F, G |
| C Major | `1(7), 2(7), 5(7), 7(7)` | Cmaj7, Dm7, G7, Bm7b5 |
| A Harmonic Minor | `1(7)` | AmMaj7: A C E G# |
| A Harmonic Minor | `3(7)` | Cmaj7#5: C E G# B |
| A Melodic Minor | `6(7)` | F#m7b5: F# A C E |

Use the existing separators and length syntax:

```text
1-6-4-5
1:1/8D, vi7, F:1bar, 5(7)
2(7):3/16
```

Each chord uses the selected default length when it has no suffix. `2(7):3/16`
lasts three sixteenths.
The editor preserves your input. Numeric cards show the resolved chord and both
degree forms, for example `Dm7` and `Degree 2 · ii7` in C Major.

### 5.3 Explicit numeric chord quality

Add a quality to select a different chord on the same scale root.

| Quality | Accepted suffixes |
|---|---|
| Major triad | `maj` |
| Minor triad | `m`, `min` |
| Diminished triad | `dim`, `o`, `°` |
| Augmented triad | `aug`, `+` |
| Major seventh | `maj7` |
| Minor seventh | `m7`, `min7` |
| Dominant seventh | `dom7` |
| Minor-major seventh | `mMaj7`, `minMaj7` |
| Diminished seventh | `dim7`, `o7`, `°7` |
| Half-diminished seventh | `m7b5`, `ø7` |
| Augmented major seventh | `augMaj7` (displayed as `maj7#5`) |

In A Minor, `5` is Em, `5maj` is E, and `5dom7` is E7. Roman `V` remains E
because uppercase Roman numerals select a major chord.

Use `(7)` for an automatic scale seventh. `27` is invalid. Separate degrees with
spaces or commas: `2, 7`. Joined progressions such as `1645` are also invalid.

Numeric syntax does not accept root accidentals (`b7`, `#4`), slash bass,
general alterations, or ninths. `m7b5` is a complete quality suffix.
`2maj(7)`, `2(7)m`, and `2(9)` are invalid. Existing absolute and Roman ninths
remain available. Parentheses are valid only for `(7)` after a bare numeric degree.

To check syntax without a DAW:

```bash
chordtext-cli --key A --mode minor "i-iv-V"
```

### 5.4 Custom note-list chords

Use square brackets to enter the notes of one custom chord. Separate notes with
commas. Spaces around the entries are allowed. Custom lists can appear between
ordinary chords, Roman numerals, and numeric chords:

```text
[1,2,4,5]
1, [1,3,5,F]:2, 6, 5
```

The list does not create a triad or seventh chord. Each entry adds one note. The
first entry is the chord root. In C Major, `[1,2,4,5]` means C, D, F, G. The card
shows `C custom`, the formula, and the notes after voicing.

An entry can be a scale degree, or a named note:

| Entry | Meaning |
|---|---|
| `1` to `7` | A note from the selected key and scale |
| `8` to `14` | Degrees 1 to 7, one octave above their generated voices; `8` is the upper octave of degree 1 |
| `b3`, `#4` | Lower or raise that scale note by one semitone |
| `1@-1`, `1@+1` | Move the generated note one octave down or up |
| `F`, `Bb`, `F#` | A named pitch class; it stays the same when the key changes |
| `F3`, `C-1` | A named note at an exact scientific pitch octave |
| `F@+1`, `Bb@-1` | Move an unpinned named pitch class relative to its generated voice |

The selected mode supplies the scale note before a `b` or `#` is applied. In C
natural minor, `3` is Eb and `b3` is D, spelled E-double-flat. Uppercase note
letters mean named notes: `B3` is the exact note B3, while lowercase `b3` is a
flattened third scale degree.

The Range and Voicing controls make the automatic baseline voices. Relative
octave entries follow those voices. An exact octave such as `F3` keeps that pitch
even if the Range or Voicing changes. Use these examples to compare the rules:

```text
[1,2,4,5]       C D F G in C Major
[1,3,5,F]       C E G F in C Major
[F3,1,3,5]      F3 is the root; the other notes use automatic voicing
[1@-1,1,8]      the tonic below, at, and above its generated voice
```

An exact note keeps its written spelling and octave. The final MIDI pitch must
still be from 0 to 127. Relative notes and added bass notes outside this range
make the chord invalid; the plugin does not clamp or omit them. Duplicate MIDI
pitches sound once. The same pitch class in different octaves remains as separate
notes.

Lists must contain 1 to 128 entries. `[1]` is valid. Empty lists, empty entries,
trailing commas, missing commas, nested brackets, and unmatched brackets are
errors. Put the length after the closing bracket, for example
`[1,2,4,5]:1/8`. The brackets keep list commas, spaces, and octave-minus signs from
starting another chord. Existing numeric chord syntax outside brackets remains
1 to 7, so `1645` is still invalid.

### 5.5 Added root bass

Choose **BASS** in the plugin, or use `--bass` / `:bass` in the CLI:

| Value | Result |
|---|---|
| `off` | Do not add a bass note (default) |
| `1` | Add the chord root one octave down |
| `2` | Add the chord root two octaves down |
| `3` | Add the chord root three octaves down |

The bass follows the chord's voiced root. For a custom chord, the first list
entry is the root. It does not follow the lowest chord note or replace a slash
bass. For example, `[1,3,5]` with BASS set to `1` adds the tonic one octave below
the chord's generated root. If the requested bass would be outside MIDI pitches
0 to 127, the progression is invalid.

The bass applies to host playback, card preview, held preview, MIDI export, drag
clips, and CLI output. It uses the chord's timing and velocity. The plugin saves
the BASS choice in each project and exposes it to automation. The computer-wide
UI scale setting is separate.

## 6. Errors and diagnostics

Every parse error identifies the failing token, a zero-based character offset
into the progression text, and a short message:

```text
Error at character <offset>: <message> (<token>)
```

For example:

```text
C, H, C99
```

produces:

```text
Error at character 3: unknown chord root (H)
Error at character 6: unknown extension "9" (C99)
```

`C99` reports `"9"` because the parser consumes the first `9` as a valid ninth
quality. The second `9` is the unknown extension.

If any token is invalid:

- no new plan is published;
- the last valid progression keeps playing;
- the chord cards show the last valid progression, with reduced brightness;
- export and drag are disabled.

The editor lists diagnostics directly below the progression input. In one-shot mode,
the CLI prints parse or pitch-resolution diagnostics and exits with code 1.

An empty progression is valid and has no diagnostics.

## 7. Grammar reference

```text
progression := separator* (token separator*)*
separator   := "," | "|" | "-" | whitespace
token       := chord (":" length)?
length      := bareBeats | noteLength | beatLength | barLength
bareBeats   := positive finite number
noteLength  := positiveInteger "/" noteDenominator ("D" | "d" | "T" | "t")?
noteDenominator := "1" | "2" | "4" | "8" | "16" | "32" | "64"
beatLength  := positive finite number ("beat" | "beats")
barLength   := positive finite number ("bar" | "bars")
chord       := absolute | roman | numeric | custom
numeric     := [1-7] ("(7)" | numericQuality)?
numericQuality := see section 5.3

custom      := "[" customEntry ("," customEntry)* "]"
customEntry := scaleNote | namedNote
scaleNote   := accidental? [1-7] octaveOffset? | [8-14]
namedNote   := pitchClass (octave | octaveOffset)?
pitchClass  := [A-G] ("#" | "b")?
octaveOffset := "@-1" | "@+1"
octave      := scientific pitch octave from -1 to 9

absolute    := root quality? alteration* ("/" bass)?
root        := [A-G] ("#" | "b")?
bass        := [A-G] ("#" | "b")?
alteration  := ("b" | "#") ("5" | "9")

roman       := accidental* numeral quality? alteration*
accidental  := "b" | "#"
numeral     := "I" | "II" | "III" | "IV" | "V" | "VI" | "VII"

quality     := see the quality table in section 3
```

Notes:

- `separator*` allows empty progressions and repeated separators. Separators inside
  a custom list are read as list content.
- `|` and `-` are separators only; they do not change timing.
- Ordinary tokens cannot contain spaces; spaces separate tokens. Spaces around
  entries inside a custom list are allowed.
- The length colon belongs to the token, for example `Cmaj7:1/8D`. The colon
  must be followed by a valid length; a trailing colon is an error.
- Literal keywords such as `D`, `T`, `beat`, and `bars` are matched
  case-insensitively.
- Absolute root accidentals come after the root letter. Roman accidentals come
  before the numeral.

## 8. Common mistakes

| Wrong | Why | Write instead |
|---|---|---|
| `C-E-G` | Hyphens are separators; this is three chords | `Cm` |
| `Cb5` | Read as a C-flat power chord | `C7b5` for C7 with a flat fifth |
| `C#5` | Read as a C-sharp power chord | `C#7#5` or `C#aug` for an augmented fifth |
| `C99` | Unknown extension after the ninth | `C9` or `C7b9` |
| `C11` | Elevenths are unsupported | `C9` or `C7` |
| `C13` | Thirteenths are unsupported | `C9` or `C7` |
| `C6/9` | The `6/9` chord is unsupported | `C6` or `Cadd9` |
| `H` | `H` is not a note name here | `B` |
| `C##` | Double accidentals are unsupported | `D` |
| `C(b5)` | Absolute chord parentheses are unsupported | `C7b5` or `Cm7b5` |
| `1645` | Numeric degrees need separators | `1, 6, 4, 5` |
| `27` | A bare digit suffix is unsupported | `2(7)` for a seventh, or `2, 7` for two chords |
| `8` | Extended degrees only work inside a custom list | `[8]` for the tonic an octave up |
| `[1,3,5,]` | A custom list cannot have an empty last entry | `[1,3,5]` |
| `F3@+1` | Exact pitches cannot also have a relative offset | `F3` or `F@+1` |
| `c` | Root letters must be uppercase | `C` |
| `V/V` | Applied/secondary numerals are unsupported | Write the target chord absolutely |
| `Cmaj7:0` | Lengths must be positive | `Cmaj7:0.5` |
| `Cmaj7:1/3` | The note denominator is unsupported | `Cmaj7:1/4` |
| `Cmaj7:1beatD` | Dotted and triplet modifiers apply only to note fractions | `Cmaj7:1/8D` |
| `C maj7` | The space splits the token | `Cmaj7` |
| `C: 2` | The space splits the token | `C:2` |

## 9. Examples by use case

### Pop loop

```text
C, G, Am, F
I-V-vi-IV
```

### Jazz cadence

```text
Dm7, G7, Cmaj7
ii7-V7-Imaj7
```

### Minor progression

```text
i, iv, V, i
```

In A minor this is Am, Dm, E, Am.

### Explicit musical lengths

```text
Cmaj7:1/8D, Am7:2beats, Dm7:1bar, G7:1/8T
```

### Slash bass

```text
C/E, F, G, Am
```

### Mixed absolute and Roman

```text
I, IV, C/E, G7
```

### Multi-line with visual bars

```text
Cmaj7:2 | Am7:2
Dm7:2   | G7:2

```

## 10. Appendix: version and scope

This guide describes the v1 syntax implemented by the plugin and the CLI.

Supported:

- absolute chord symbols with the qualities and alterations listed above;
- Roman numerals in major and minor keys;
- slash bass on absolute chords;
- per-chord musical lengths;
- custom note-list chords and optional root bass;
- mixed absolute and Roman progressions;
- the separators listed above.

Not supported in v1:

- 11th and 13th chords;
- `6/9` chords;
- applied or secondary numerals such as `V/V`;
- slash bass on Roman numerals;
- double accidentals;
- parenthesised alterations;
- relative octave offsets other than one octave;
- a piano-roll chord editor or inferred chord quality for custom lists;
- key changes or modulation inside one progression.

For the full feature and platform limits, see the repository
[README](README.md).
