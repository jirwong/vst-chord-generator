# Flexible chord lengths

Date: 2026-10-24
Status: Draft for approval. Individual decisions below were agreed during brainstorming. The full design has not yet been approved. Product implementation has not started.

Builder review: Completed on 2026-10-24. No important unanswered decisions were identified.

## 1. Purpose and scope

Let users select a default chord length and enter musical note lengths, beats, or bars for individual steps. Apply the same rules to the plugin and CLI. Keep the existing C++17/JUCE architecture and compact Chordscript design.

These lengths define how long each progression step sounds. They do not create repeated attacks, strumming, rests, ties, or a new rhythm generator. Manual left-press preview lasts until release, and right-click hold lasts until toggled or cancelled under the existing rules. A progression duration does not become a timeout for those gestures.

Items marked **Codex decision** are proposed details supplied by Codex. They are part of this draft for review, rather than decisions already made by the user.

## 2. Decisions agreed with the user

- Add a Default length control under the main settings row, beside Bass.
- Apply the default only to steps without an explicit length suffix.
- Keep existing bare numeric suffixes in quarter-note beats. `C:2` remains two beats regardless of the default or time signature.
- Support half, quarter, eighth, and sixteenth notes, each with straight, dotted, and triplet forms.
- Support additional entered lengths such as `3/16`, beats, and bars.
- Resolve bar lengths from the DAW's current time signature. Use 4/4 when the DAW does not supply a valid signature.
- The CLI defaults to 4/4 and supports an explicit time signature.
- Save Default length per plugin instance. Old projects restore a four-beat default.
- Update bar-based lengths when the host signature changes, including during playback.
- Apply default-length changes during playback to all steps that use the default. Explicit lengths keep their meaning; an explicit bar length still follows the host signature.
- During a timing change, release the old notes and resume at the chord that belongs at the current DAW position under the revised progression. Do not restart the DAW or move its playhead. The active chord may change.
- Each card shows its written/selected length and resolved beat count. Cards that inherit the default also show DEFAULT.

The user selected the under-settings placement from the interactive mockup:
`C:/Users/jirwo/.codex/visualizations/2026/10/22/01a165c6-3b92-7423-8304-f20cee5b321a/chordscript-length-controls.html`.

The mockup's time-signature selector is a preview aid. It is not a proposed plugin control. The plugin follows the DAW; the CLI supplies its own signature.

## 3. Meaning of lengths

One internal beat always means one quarter note, including in compound meters such as 6/8. It does not mean a dotted-quarter pulse.

For a note fraction N/D, the length in beats is `4 * N / D`. Dotted lengths multiply that by 3/2. Triplet lengths multiply it by 2/3.

| Input | Meaning | Quarter-note beats |
| --- | --- | --- |
| `1/2` | Half note | 2 |
| `1/2D` | Dotted half | 3 |
| `1/2T` | Half triplet | 4/3 |
| `1/4` | Quarter note | 1 |
| `1/4D` | Dotted quarter | 3/2 |
| `1/4T` | Quarter triplet | 2/3 |
| `1/8` | Eighth note | 1/2 |
| `1/8D` | Dotted eighth | 3/4 |
| `1/8T` | Eighth triplet | 1/3 |
| `1/16` | Sixteenth note | 1/4 |
| `1/16D` | Dotted sixteenth | 3/8 |
| `1/16T` | Sixteenth triplet | 1/6 |
| `3/16` | Three sixteenths | 3/4 |
| `1beat` | One beat | 1 |
| `1bar` | One bar | numerator * 4 / denominator |

Examples: one bar is four beats in 4/4, three beats in 3/4, three beats in 6/8, and 3.5 beats in 7/8.

All notes in a step, including optional root bass, share that step's start and end. Consecutive steps retain the existing note-off-before-note-on ordering, including shared pitches.

## 4. Text syntax

Keep the existing colon suffix and progression separators:

```text
1, 4, 5:1/4, 1:1bar
C:1/8, Dm:1/8D, G:1/4T, C:2beats
[1]:1/16, [3]:1/16, [4]:1/8T, [5]:1bar
[1,3,5,F]:3/16, 6:1.5beats, 5:3bars
```

The suffix applies to chord symbols, Roman numerals, numeric chords, custom chords, and single-note custom steps.

**Codex decisions for the initial grammar:**

- Bare positive finite numbers retain the current beat-based syntax and behavior, including decimals already accepted by the parser.
- Note fractions have a positive integer numerator and a denominator from 1, 2, 4, 8, 16, 32, or 64. Thus `3/16` and `5/8` are valid. Menu presets are a smaller, convenient selection from this grammar.
- A fraction may have one optional D or T suffix. Lowercase d/t aliases are accepted; the display uses uppercase D/T. A combined suffix such as DT is invalid.
- Beat and bar forms take a positive finite number plus `beat`, `beats`, `bar`, or `bars`. Decimal amounts such as `1.5beats` and `0.5bar` are valid. Unit words are case-insensitive.
- Typed suffixes are compact: `C:1beat` and `C:1bar`. Spaces are allowed around progression tokens, but not inside a length suffix. The menu can display natural labels such as “1 beat”.
- D/T modifiers apply to note fractions only. `1beatD`, zero/negative lengths, unsupported denominators, missing operands, extra slashes, and trailing junk are errors.
- A slash inside the duration suffix must not be read as a chord's slash bass. Brackets continue to protect custom-list commas and octave-minus signs.
- Preserve source positions for useful diagnostics. Retain both the requested length expression and whether it came from the default; a resolved beat count alone loses information needed for meter changes and card labels.

An explicit length never follows the selected Default length. Bar expressions follow the effective time signature; note fractions and beat expressions do not.

## 5. Default length control and saved state

**Codex decision:** Use a grouped choice menu with 19 stable choices:

- Notes: `1/2`, `1/2D`, `1/2T`, `1/4`, `1/4D`, `1/4T`, `1/8`, `1/8D`, `1/8T`, `1/16`, `1/16D`, `1/16T`, `3/16`.
- Beats: `1 beat`, `2 beats`, `4 beats`.
- Bars: `1 bar`, `2 bars`, `4 bars`.

Use **4 beats** for new instances and old projects without this setting. Four beats and one bar remain distinct choices even when they currently resolve to the same duration.

**Codex decision:** Append an automatable APVTS choice parameter with stable ID `defaultLength`. Preserve existing parameter IDs, values, ordering, and legacy mode migration. Document a permanent choice mapping before implementing it; future choices must not renumber existing values. Save progression text exactly as entered, without expanding inherited lengths into suffixes.

The host signature is runtime context. Do not save it as a plugin-selected meter or make it a computer-wide preference. Before any host context is available after loading a project, use the agreed 4/4 fallback.

## 6. DAW signature and changes during playback

**Codex decisions:**

- Use one effective current signature for the whole resolved progression. This feature does not scan a future host meter map or calculate a different meter for each step.
- Missing or invalid current host signature means 4/4, even after a previously valid signature. A missing signature does not by itself disable playback when PPQ and BPM are otherwise available.
- Validate the numerator as 1–255 and the denominator as one of 1, 2, 4, 8, 16, 32, 64. Use the same accepted signature domain for the CLI. Invalid CLI values are errors; invalid host values use the fallback.
- Capture the effective signature only where the host playhead is legally readable: `processBlock`. Publish a coherent numeric snapshot or revision for message-thread consumption. Do not read numerator and denominator as an uncoordinated pair.
- A processor-owned message-thread mechanism must consume changes even when the editor is closed. Never build or publish a plan, parse text, write files, or introduce allocations in the audio callback.
- “Immediately” means the next coherent rebuilt plan adopted by an audio block, subject to message-thread delivery. It does not promise sample-accurate reconstruction at a meter event inside an already supplied block.
- Continue the current plan while a valid timing update is pending. When the replacement is adopted, release old notes first and resynchronize at the current host PPQ position using the existing song-origin and loop behavior.
- With Loop off, a revised progression that already ended at the current position produces no replacement note. With Loop on, use the revised loop length to calculate the current phase.
- Coalesce rapid changes. An obsolete rebuild must not clear a newer pending request or publish apparently current labels, preview data, or file metadata.
- A timing rebuild uses the existing preview cancellation/revision rules. Cancel a running manual preview or hold when its musical snapshot is invalidated; ordinary left-release and right-toggle behavior stays as defined in the preview feature.

For example, `C:1bar, G:1bar` changes from eight beats in 4/4 to six beats in 3/4. At host PPQ 7 with Loop on, the revised sequence is at phase 1 and plays C. With Loop off, it has ended. A numeric `C:4` still lasts four beats.

## 7. UI

Keep the carbon charcoal palette, subtle teal accent, compact layout, Loop margin, export button heights, and monochrome piano icon.

- Place DEFAULT LENGTH under the main settings row beside BASS, as selected by the user.
- Show the current effective host signature in compact status text. Identify the fallback when the signature is unavailable.
- Show the selected/written length and resolved beat count on each card. For example, `1/8D` and `¾ beat`, or `1 BAR` and `3 beats` in 6/8.
- Show a small DEFAULT label only when that step inherits its length.
- Keep bass information and PREVIEW/HELD badges readable within the card.
- Recalculate progression totals, card timing, current-step feedback, and drag subtitles from the same published timing snapshot. Any bar count must use the effective signature, rather than assuming four beats.
- Preserve compact valid status, nearby errors, and the existing empty/editing/export/drag/unavailable-transport behavior.

**Codex decisions:** Target the existing 960 × 650 logical window. Use available vertical spacing and existing scrolling; do not enlarge the window for this proposal. Keep card content readable at 75%, 100%, 125%, and 150%. Use readable common fractions where practical; retain the existing compact numeric formatter for other values without changing their actual timing. The builder can settle tooltip details and text truncation.

The mockup's alternate “beside progression” placement was not selected. The final UI uses the under-settings placement.

## 8. Export and drag clips

**Codex decisions:**

- Export and drag use the same current resolved progression as live generation. The plan's timing context and file settings must agree.
- Before writing, consume or rebuild against the latest available coherent length/meter snapshot on the message thread. If another update invalidates that snapshot, use a safe retry or report pending timing; do not combine old note times with new signature metadata.
- Capture one snapshot per file write. A later host change affects subsequent output, not a file already being written or a clip already given to the DAW.
- Exported files include tempo and one effective time-signature event at tick zero. Replace the writer's hard-coded 4/4 event with validated settings. The file does not contain a future host meter map.
- Drag clips continue to omit tempo and time-signature metadata. Their note lengths are already resolved, so they still reflect the chosen timing snapshot.
- Keep the shared MIDI writer and its 960 ticks per quarter note. All requested note-menu presets, including triplets, are exactly representable at that resolution. Preserve existing rounding for legacy numeric beat lengths.

This metadata change applies to plugin exports and CLI exports. The writer's default settings still produce 4/4 for existing callers that do not supply a signature.

## 9. CLI

**Codex decisions:** Add matching one-shot and interactive settings:

```text
--default-length <length>       Default: 4beats
--time-signature <N/D>          Default: 4/4
:default-length <length>
:time-signature <N/D>
```

Examples:

```text
chordtext-cli --key C --mode major --default-length 1/8D "1, 4, 5:1bar"
chordtext-cli --time-signature 6/8 "[1]:1/8T, [3]:1/8T, [5]:1bar"
```

The CLI default can use any valid length expression in the shared grammar, including fractions not offered as plugin presets. Interactive setting changes apply to later reports and MIDI output. The existing explicit export of the last valid input re-resolves that text with the current length and signature settings.

Keep existing JSON fields and their meanings, including numeric `startBeat` and `durationBeats`. Add canonical default-length and time-signature settings, and per-row length expression/default-inheritance metadata. Human output shows the written/selected length with its resolved beats. Help and settings summaries expose both settings.

The exact additive JSON field names and internal C++ interfaces are left to the implementation planner. Do not duplicate duration parsing or conversion in the CLI.

## 10. Errors and compatibility

- Preserve the plugin's last valid host playback plan and learning cards on invalid text. Disable export/drag and prevent manual preview from starting from stale cards.
- Empty text remains valid and clears the plan and cards.
- CLI errors report only current invalid input and do not write automatic MIDI output.
- Legacy projects use four-beat defaults. Legacy bare numeric suffixes preserve their quarter-note-beat meaning in every meter.
- Preserve existing key/mode/voicing/range/velocity/loop/scale/bass behavior, state migration, triple-buffer ownership, and preview epoch/revision safeguards.
- Expose clear errors for invalid new length syntax. Do not silently reinterpret malformed note fractions as bare beat values.
- Parsing and musical conversion stay in the JUCE-free theory layer. Register new files in the existing shared CMake source lists.

The planner must address positive finite resolved lengths, safe totals, supported fraction parsing, file tick conversion, and scheduling limits. Retain legacy behavior where safe; surface any unavoidable compatibility change for review rather than silently narrowing the old numeric grammar.

## 11. Required implementation verification

The implementation plan must cover:

- Every preset and explicit fraction, dotted/triplet conversion, unit alias, numeric legacy length, and inherited/default distinction.
- Mixed chord syntax, custom-list separators, slash chords, source offsets, invalid fractions/units/modifiers, and duration errors.
- 4/4, 3/4, 6/8, 7/8; missing/invalid host meter; CLI meter validation; note/beat invariance and bar changes.
- Default and host-meter changes during playback, Loop on/off, seek/stop, editor closed, rapid changes, and old-note release before new notes.
- Pending update/export coherence, stale async rebuilds, preview cancellation, saved state, old-state migration, and automation mapping.
- Agreement among plugin rows, live MIDI, exported MIDI, drag MIDI, CLI reports, and CLI MIDI; real file signature bytes and metadata omission for drag.
- Card text, bass line, DEFAULT/PREVIEW/HELD labels, empty/valid/invalid/editing/playback/export/drag/unavailable-transport states, and all UI scales.
- The Windows Release VST3 build, TheoryTests, ProcessorTests, CliTests, meaningful CLI checks, and `git diff --check`.

DAW routing, live meter-change behavior, automation, and drag acceptance require the README manual host checks. Report unperformed checks. During this design phase, no product build or suite run is required.

## 12. Next step

A fresh builder review found no important unanswered decisions. The user now reviews the complete design, including the Codex decisions. After approval, prepare a concrete implementation plan. Product implementation follows plan approval.
