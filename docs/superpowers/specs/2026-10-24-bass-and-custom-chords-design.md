# Optional bass and custom chords

Date: 2026-10-24
Status: Approved by the user on 2026-10-24. Product implementation has not started.

## 1. Purpose

Let users add a root bass to a progression and write their own chords as lists of notes. Keep the current C++17/JUCE architecture and the compact Chordscript design. Plugin playback, card preview, export, drag clips, and CLI output must use the same resolved notes.

The user approved the full document after the design review. Sections marked **Codex decision** identify details originally recommended by Codex and included in that approval.

## 2. Bass control

- Add a BASS menu: Off, 1 octave down, 2 octaves down, 3 octaves down.
- Default: Off. One setting applies to the whole progression.
- Follow the chord root, not the lowest chord tone or a slash bass.
- Subtract 12, 24, or 36 semitones from the root's actual voiced MIDI pitch.
- Example: the chord E4–G4–C5 has voiced root C5. One octave down adds C4, even though E4 is lower than C4.
- For a custom chord, the first entry defines the root. If the first entry is F3, one octave down adds F2.
- Apply bass to host playback, left-press preview, right-click hold, exported MIDI, and dragged MIDI.
- Save the setting per plugin instance in the DAW project. Expose it to host automation. UI scale remains a separate computer-wide preference.

**Codex decisions:** The bass uses the chord's velocity, MIDI channel, start time, and duration. Add it after chord voicing, so it does not affect voice leading. If it duplicates another final pitch, emit that pitch once. Keep bass metadata so the card can still show the requested bass.

## 3. Custom chord syntax

Write a comma-separated note list in square brackets. Spaces around entries are allowed. Mix custom chords with existing chord symbols, Roman numerals, and numeric chords.

Examples in C major:

| Input | Meaning |
| --- | --- |
| `[1,2,4,5]` | C, D, F, G |
| `[1,3,5,F]` | C, E, G, F |
| `[F3,1,3,5]` | Exact F3 root, plus automatically voiced C, E, G |
| `1, [1,2,4,5]:2, 6, 5` | Existing chords mixed with a two-beat custom chord |

Each number inside brackets selects one note from the selected key and scale. It does not generate a triad or seventh chord. The first entry is the root, whether it is a degree or a named note. Custom chords use the existing Voicing and Range controls, subject to the pitch overrides below.

Named notes keep their pitch class when key or scale changes. For example, `[1,3,5,F]` changes from C/E/G/F in C major to D/F#/A/F in D major.

### 3.1 Degrees and accidentals

- Degrees 1–7 select the seven notes of the selected scale.
- Degrees 8–14 select those same notes one octave above their generated baseline voices. Thus 8 is octave-up degree 1 and 9 is octave-up degree 2.
- `1@-1` selects degree 1 one octave below its generated baseline voice.
- A single `b` or `#` before a degree lowers or raises the selected scale note by one semitone. This uses the selected mode's note, not a parallel major scale.
- Example in C natural minor: `3` is Eb; `b3` sounds D. Preserve theoretical spelling where possible.
- These expanded degrees apply inside brackets only. Existing numeric chord syntax outside brackets remains 1–7, with its existing triad/seventh rules. `1645` remains invalid.

**Codex decisions:** Accept `@+1` as the upward counterpart of `@-1` on degrees 1–7 and on named notes without an exact octave. Do not accept relative suffixes on degrees 8–14 or on exact octave notes. This prevents conflicting or compounded offsets. The available relative offsets are exactly -1 and +1; no `@-2`, `@+2`, or `@0` in this release.

### 3.2 Named notes and exact octaves

- Use uppercase A–G for named notes, with one optional `b` or `#`: `F`, `F#`, `Bb`.
- Add a scientific pitch octave to pin an exact pitch: `F3`, `F#4`, `C-1`.
- An exact pitch takes priority over Range and Voicing. F3 remains F3 even when the rest of the chord moves.
- An unpinned note follows the generated voicing. Relative offsets follow that baseline: if degree 1 moves from C4 to C5, degree 8 moves from C5 to C6.

**Codex decisions:** Case distinguishes `b3` (flat scale degree) from `B3` (exact B3). Exact notes must resolve to MIDI 0–127, from C-1 through G9. Reject double accidentals, lowercase note letters, and unsupported octave forms with useful diagnostics. No rests, nested brackets, chord-quality tokens, or slash syntax inside a custom chord in this release.

### 3.3 List and duration rules

**Codex decisions:** A one-note list such as `[1]` is valid. Empty lists, empty entries, missing commas, trailing commas, unmatched brackets, and nested brackets are invalid. Limit each custom chord to 128 entries. Use the existing positive `:beats` duration suffix after the closing bracket; default duration remains four beats. The progression scanner must protect commas, whitespace, and minus signs inside brackets, including `@-1` and `C-1`, while preserving existing separators outside brackets. Report errors at the offending entry or bracket in the original text.

## 4. Voicing and root identity

The result must preserve deliberate octave differences without making automatic voice leading ambiguous.

**Codex decision — shared resolution algorithm:**

1. Resolve each entry to a spelled pitch class and either an automatic pitch, a relative octave offset, or an exact MIDI pitch. Retain the first entry as root metadata.
2. Make an automatic baseline chord from unique pitch classes. Put the root first, then sort the other pitch classes by ascending semitone distance from the root. Entries after the first do not pin a voice order.
3. Use the existing Range/Voicing algorithm on this baseline chord. Each pitch class has one baseline voice.
4. Resolve every original entry from its corresponding baseline voice. Apply its relative offset, or replace it with its exact pitch. Example: if baseline C is C4, `[1@-1,1,8]` produces C3/C4/C5.
5. Keep the resolved pitch of the first entry as the voiced root, including its exact octave or relative offset. Do not replace it with the lowest occurrence of the root pitch class.
6. Add optional bass relative to that root. Sort final MIDI pitches and merge identical pitches. Different octaves remain separate notes.

Use the baseline chord voices as the previous chord for the next baseline voice-leading decision. Deliberate offsets, pinned notes, and added bass do not enter that baseline history. Existing non-custom chords retain their current voicing behavior; their root anchor is the actual voiced occurrence of their root. For slash chords use the voiced root tone, not the slash-bass tone. If a baseline contains repeated occurrences of the root, use its lowest voiced root occurrence for ordinary chords; custom chords always use the first entry's resolved pitch.

The custom chord representation must store its supplied tones explicitly. It must never silently construct a major triad from an empty or unrecognised quality string.

## 5. MIDI bounds and invalid states

**Codex decision:** Validate the final requested pitches before building a render plan. If an exact note, relative note, or added bass is outside MIDI 0–127, the current result is invalid. Report the affected chord and a useful explanation. Do not clamp, transpose, omit, or silently wrap the requested note. Range controls guide automatic baseline voicing; relative offsets, exact notes, and bass may extend outside that chosen range while remaining within MIDI limits.

Preserve current plugin invalid-input behavior: keep the last valid host playback plan and learning cards. Mark the current result invalid near the progression editor, disable export and drag, and prevent preview or hold from starting from stale cards. Release any manual preview/hold when musical settings or text change, using the existing cancellation mechanism. A stale card must not appear to represent the invalid input. Empty text is valid and clears the plan and cards.

The CLI reports only the current request. Invalid input or pitch resolution must fail without producing stale chords or an automatic MIDI file. Use the existing failure/exit conventions. Preserve the interactive session's explicit `:export` command for its last valid progression; re-resolve that progression under the current settings and refuse the write if those settings now cause a pitch error.

## 6. UI design

Use the approved mockup:

`C:/Users/jirwo/.codex/visualizations/2026/10/22/01a165c6-3b92-7423-8304-f20cee5b321a/chordscript-bass-custom-chords.html`

- Keep the logical window at 960 × 650. No resize is needed for this design.
- Place BASS on the right of the scale-summary row, below the main settings row.
- Keep the current charcoal palette, subtle teal accent, compact spacing, action button heights, drag keyboard icon, Loop alignment, and UI scale behavior.
- Show custom cards as `<root name> custom`, for example `C custom` or `F custom`. Do not infer a major/minor quality.
- Show the custom note formula in the analysis line and the actual voiced chord notes in the notes area.
- When bass is on, show a separate compact `Bass: C2` line with the resolved bass pitch. Do not duplicate it in the chord-only notes line.
- Keep preview and held badges within their existing bounds.
- Collapse diagnostics to the compact status row when valid. Show errors beside the progression editor when invalid.

**Codex decisions:** Allow formula and note text to truncate within a card and expose the full text through the existing tooltip pattern. Keep the root and bass readable. Expand the scale-summary row from 22 to 28 logical pixels to accommodate the menu, using the existing flexible gap. Preserve card scrolling for long progressions. Check empty, valid, invalid, editing, playing, preview, held, exporting, dragging, and unavailable host-transport states at all supported UI scales. Custom syntax and bass do not introduce a new transport mode; preview continues to use the existing transport-independent audition path.

## 7. CLI parity

Add custom chord syntax to one-shot input and interactive input. Add `--bass off|1|2|3`, default `off`, and interactive `:bass off|1|2|3`. Settings summaries and help show the active bass choice.

**Codex decisions:** Keep existing report fields and add `bassOctaves` (0–3) to JSON settings, plus nullable `bassMidi` and `bassNote` to each chord row. Existing `voicing` and `notes` report the full final sounding result, including bass; `spelledTones` describes the chord itself. Human output includes the bass setting and a Bass column when enabled. Custom rows use `<root> custom` for `symbol` and `custom` for `degree`; their `source` preserves the supplied formula. These additions must not change existing field meanings or existing output when bass is off, apart from the new JSON fields and settings indication.

Example: `chordtext-cli --key C --mode major --bass 1 "1, [1,3,5,F]:2, 6, 5"`.

## 8. Architecture and compatibility

- Keep parsing, spelling, voicing, bass resolution, diagnostics, and final MIDI pitch generation in the JUCE-free theory layer.
- Both plugin and CLI use this shared result, then the existing render-plan and MIDI-file pipeline. UI code must not duplicate music rules.
- Append a new stable four-value APVTS parameter `bassOctaves`, values 0–3. Do not change existing IDs, parameter ordering, legacy mode migration, or saved progression text format. Old state without the parameter restores Off.
- Rebuild and publish plans on the message thread. Preserve triple-buffer ownership and the preview epoch/revision safeguards. The bass parameter must invalidate cached musical data and cancel manual notes like other musical parameters.
- Do not add file I/O, plan building, or allocations to the audio callback.
- Preserve export tempo/4/4 metadata and drag metadata omission. Retain note-offs before note-ons at equal times.
- Update `SYNTAX.md` as the canonical grammar and README for UI/CLI use. These design records do not override implemented behavior.

Exact C++ types, helper names, UI attachment methods, and file splits are left to the implementation planner, provided these contracts hold. Add any new files to the existing shared CMake source lists.

## 9. Acceptance and later verification

The implementation plan must include verification for:

- All nine modes; degree accidentals; named accidentals; fixed named pitches after key changes.
- Mixed progression syntax and durations; bracket-local separators; malformed lists and useful source offsets.
- Relative upper/lower octaves, exact pitches, repeated pitches, first-entry root tracking, and one-note chords.
- Both voicing modes and all ranges, including baseline movement, exact-note overrides, root/slash-bass distinction, and MIDI-bound errors.
- Bass Off/1/2/3, root above the lowest tone, custom roots, duplicates, same timing/velocity, and voice-leading isolation.
- Equality of plugin playback, manual preview/hold, export, drag, CLI reports, and CLI MIDI output.
- Parameter automation, saved-state round trips, old-state default Off, preview cancellation, and invalid-to-valid/empty transitions.
- UI layout at 75%, 100%, 125%, and 150%, with diagnostics and long formulas.

After implementation, build the VST3 and run TheoryTests, ProcessorTests, and CliTests under the repository's Windows/VS2022 instructions. Report results and compiler warnings. DAW playback, routing, host automation, and drag acceptance need the README manual checks; explicitly list any host checks not performed. No product build or test run is required during this design phase.

## 10. Scope and next step

This feature does not add a piano editor, per-chord bass settings, independent bass rhythm/velocity, chord-quality inference for lists, arbitrary multi-octave relative offsets, or new scales.

Next: a concrete implementation plan for review. Product implementation follows plan approval.
