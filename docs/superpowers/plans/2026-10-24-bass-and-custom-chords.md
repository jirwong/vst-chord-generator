# Optional Bass and Custom Chords Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add custom note-list chords and an optional root bass consistently across Chordscript and its CLI.

**Architecture:** Extend the existing theory model with explicit custom-note entries and bracket-aware parsing. A shared JUCE-free resolver runs baseline voicing, applies pitch overrides, validates MIDI bounds, and adds bass. The plugin and CLI consume that result through the existing render-plan and MIDI writer; audio-thread scheduling and preview exchanges keep their present ownership rules.

**Tech Stack:** Windows x64, C++17, JUCE 8.0.15, CMake 3.22+, Visual Studio 2022 Build Tools, existing CHECK-based test suites.

**Spec:** `docs/superpowers/specs/2026-10-24-bass-and-custom-chords-design.md` (approved). Read it before implementing this plan.

## Global Constraints

- Logical window: 960 × 650; UI scale choices: 75%, 100%, 125%, 150%.
- Bass: Off, 1 octave down, 2 octaves down, 3 octaves down; default Off; stable appended APVTS ID `bassOctaves`, values 0–3.
- Degrees 1–14 inside brackets only; relative suffixes exactly `@-1` and `@+1`, only on degrees 1–7 or unpinned named notes. Maximum 128 entries per list.
- Exact and final pitches must be MIDI 0–127. Do not silently clamp, omit, or transpose a requested pitch.
- Preserve existing APVTS IDs/order, old-project scale migration, progressionText, and ordinary chord behavior with bass Off.
- Music resolution stays JUCE-free and shared. Plan building/publication and file writes stay on the message thread; AudioPlayHead::getPosition() stays processBlock-only.
- Preserve triple-buffer ownership, preview epoch/revision checks, invalid-input last valid host plan, and empty-input clearing.
- Export includes tempo and 4/4 metadata; drag clips omit both. Equal-time note-offs precede note-ons.
- Use the Visual Studio 17 2022 generator with `-A x64`. Do not use Ninja or CTest. Never permanently delete files; use trash-cli if removal is necessary.
- This document is a plan only. Implementation, builds, and test runs follow plan approval. At execution, repository guidance supplies authorization to run the verification listed here.

## Review Focus

1. `B#3`/`Cb4` and enharmonic duplicates: scientific octave and root spelling must survive semitone wrap (Tasks 1–2).
2. Restoring an old project into an already bass-enabled instance must restore Off, not retain the instance's prior value (Task 3).
3. Pending bass automation while a chord is held must release old notes and reject stale card gestures before the deferred rebuild (Task 3).
4. A duplicate bass tone must sound once but still appear in the Bass label; upper/lower C entries must stay distinct (Tasks 2, 4–5).
5. An invalid interactive input must not overwrite the CLI's last valid text; explicit export must re-resolve that text with the current bass settings (Task 4).

## File Map and Dependencies

| Files | Responsibility |
| --- | --- |
| `Source/Theory/Chord.h`, `Chord.cpp` | Custom entries, explicit baseline tones, truthful custom labels, octave-aware pitch spelling |
| New `Source/Theory/CustomChordParser.h`, `.cpp` | Bracket-entry grammar and source-local diagnostics |
| `Source/Theory/Progression.cpp` | Bracket-aware token scanning, existing duration parsing and parser dispatch |
| New `Source/Theory/ChordResolution.h`, `.cpp` | Shared baseline/override/bass resolution and MIDI diagnostics |
| `Source/Theory/Voicer.h` | Document baseline-only voicing API; retain existing algorithm |
| `Source/Plugin/PluginProcessor.h`, `.cpp` | Parameter/state compatibility, final-plan/cache integration, learning metadata |
| `Source/Plugin/PluginEditor.h`, `.cpp` | Bass control, compact card layout, full-text tooltips |
| `Source/Cli/CliSettings.h`, `.cpp`, `CliOptions.cpp`, `CliSession.cpp`, `CliReport.h`, `.cpp` | CLI bass settings, shared resolution, reports and export |
| `Tests/TheoryTest.cpp`, `Tests/ProcessorTest.cpp`, `Tests/ChordPreviewChecks.cpp`, `Tests/CliTest.cpp` | Existing suite entry points and regression checks |
| `CMakeLists.txt` | Register new theory implementations in THEORY_SOURCES only |
| `SYNTAX.md`, `README.md` | Canonical grammar, UI/CLI examples, manual host checklist |

Task 1 → Task 2 → Task 3 → Task 4 → Task 5 → Task 6. CLI Task 4 technically depends only on Task 2; keep this order for a single working branch. Do not split this into unrelated feature plans: both user controls depend on the same final-note resolver and output consistency.

## Execution Setup and Test Commands

After approval, use the worktree skill to select an isolated checkout, starting from the then-current main branch with a `codex/` branch. Inspect the worktree attachments before creating one. Carry the approved spec and plan into that checkout, preserving any other local work. Do not implement on main or discard changes.

Use existing tests and their CHECK macro, not a new framework. Test functions named below are local functions called by the existing suite main/runner; they are not command-line filters. For each task, write the listed checks, observe a focused failure, implement, rerun, then commit only that task's files. A compiler error for a new interface is an acceptable initial red result; after the interface exists, verify the behavioral checks fail before filling in the body. Never count a stale executable as a successful run after a failed build.

PowerShell focused build (substitute the target):

```powershell
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build build --config Release --target TheoryTests --parallel
& ./build/Tests/TheoryTests.exe
```

Other targets/executables: `ProcessorTests`, `CliTests`, and `ChordTextCli` → `build/Cli/chordtext-cli.exe`. Run the executable only after the build exits 0. If build configuration is absent, use the documented build wrapper and VS generator; obtain JUCE 8.0.15 as instructed by AGENTS.md. Do not force reconfigure by permanently deleting build files.

### Task 1: Represent and Parse Custom Chords

**Files:** Create CustomChordParser.h/.cpp; modify Chord.h/.cpp, Progression.cpp, CMakeLists.txt; test in Tests/TheoryTest.cpp.

**Interfaces (namespace chordtext):**

```cpp
struct CustomNoteEntry {
    PitchClass pitchClass;
    int octaveOffset = 0;
    std::optional<int> exactMidi;
    std::string sourceText;
    std::size_t offset = 0; // absolute progression byte offset
};
// Append to ChordSpec, after existing fields; nullopt means ordinary chord:
std::optional<std::vector<CustomNoteEntry>> customNotes;

struct CustomChordParseResult {
    std::optional<ChordSpec> spec;
    std::vector<Diagnostic> diagnostics;
};
CustomChordParseResult parseCustomChord(std::string_view text,
                                      const Key& key, std::size_t offset);
std::string voicedNoteName(const PitchClass& spelling, int midi);
```

Consumes existing `Key::degree(int)`, PitchClass, Diagnostic, Chord and `parseProgression(text, key)`. Produces a ChordSpec rooted at its first entry. Parser text includes brackets but excludes duration. `Chord::tones()` returns the unique root-first baseline pitch classes sorted by root-relative semitone distance for custom chords, and retains existing ordinary behavior. Preserve every original entry in customNotes, including enharmonic duplicates and source positions. `Chord::symbol()` returns `<root> custom`; `degreeLabel()` returns `custom`. `voicedNoteName` computes the octave from the supplied spelling's unwrapped semitone, so `B#3` remains B#3 rather than B#4. Do not change the existing pitch-class-only noteNameForPitch contract.

- [ ] **1. Write failing `checkCustomChordParsing()` and `checkCustomGrammarDiagnostics()` checks.** Assert C major `[1,2,4,5]` has four entries C/D/F/G and C root; `[F3,1,3,5]` has exact first MIDI 53 and F root; `[1@-1,1,8,14,F@+1]` has offsets -1/0/1/1/1. Assert `B3` is exact 59 and `b3` is scale-altered degree 3. Test every mode's seven degrees against Key::degree, with single b/# applied after scale selection. C minor b3 sounds D and retains Ebb spelling. Verify F stays F across key changes.
- [ ] **2. Add scanner and boundary assertions.** `C, [C-1, 1@-1, F#4]:2 | V` is three steps of 4/2/4 beats. Check degree 0/15, `8@-1`, `F3@+1`, `1@0`, nested/missing brackets, trailing/empty entries, lowercase notes, double accidentals, missing comma, suffix junk, and 129 entries reject with correct token/source offset. A 128-entry list and `[1]` succeed. Existing `1645` remains invalid. Exact C-1/G9 valid, B-1 is11 and valid; Cb-1 below0 or A9 above127 invalid. B#3=60 and Cb4=59, including spelled names. Root selection survives enharmonic duplicates.
- [ ] **3. Build/run TheoryTests; record expected new failures.** Do not weaken old checks to pass.
- [ ] **4. Implement the interfaces.** Parse integers with bounded conversion; accept exact octaves -1 through9 only when resulting MIDI is valid. Protect inner separators, reject unmatched closing brackets and invalid text glued to a closing bracket, and preserve ordinary token/duration behavior. Custom Chord construction bypasses quality-interval generation. Register CustomChordParser.cpp in THEORY_SOURCES. Add the named test functions to the existing main.
- [ ] **5. Rebuild/run TheoryTests.** Expected exit0, 0 failures, old parsing checks intact. Commit `feat: parse custom note-list chords` with only this task's files.

### Task 2: Resolve Final Chord and Bass Pitches Once

**Files:** Create ChordResolution.h/.cpp; modify Voicer.h comments and CMakeLists.txt; test in Tests/TheoryTest.cpp.

**Interfaces (namespace chordtext):**

```cpp
struct ResolvedChord {
    std::vector<int> chordPitches; // sorted, unique; before added bass
    std::vector<int> pitches;      // sorted, unique; complete output
    int rootMidi = 0;
    std::optional<int> bassMidi;
};
struct ChordResolutionResult {
    std::optional<std::vector<ResolvedChord>> chords;
    std::vector<Diagnostic> diagnostics;
};
ChordResolutionResult resolveProgression(const Progression& progression,
                                        const VoicingSettings& settings,
                                        int bassOctaves);
```

Consumes Task 1's customNotes and explicit tones plus existing `voiceChord(const Chord&, const optional<vector<int>>&, const VoicingSettings&)`. Produces one resolved chord per progression step, or diagnostics with no partial chords. Bass0 means Off. Invalid bass values outside0–3 produce a diagnostic. Empty progression succeeds with an empty vector. Existing low-level voiceChord/voiceProgression remain baseline operations; plugin and CLI must call resolveProgression for final notes.

- [ ] **1. Write failing `checkCustomChordResolution()` checks.** With rootPosition/range48–84, `[1@-1,1,8]` yields chordPitches `{36,48,60}`, root36; `[F3,1,3,5]` keeps root53 despite automatic F baseline/range. Assert the root follows the first entry for `[8,1]`. Test both voicing modes and ranges36–72/48–84/60–96. Compare automatic pitch-class voices with independent baseline voiceChord calls; move the baseline between successive chords and assert relative entries move12 while exact F3 stays53. Compare subsequent baseline choices with the same baseline sequence without overrides or bass.
- [ ] **2. Write failing `checkRootBassResolution()` and `checkResolutionBounds()` checks.** For pinned `[C5,E4,G4]`, bass1/2/3 is60/48/36; bass1 complete output is `{60,64,67,72}`. For `[F3,1,3,5]`, bass1 is41. For `[C4,C3]` plus bass1, final `{48,60}` has no duplicate but bassMidi remains48. Test ordinary C/E uses voiced C, not E; ordinary root/extended/slash chords with bass0 have the same pitch multiset as before. A custom exact G9 remains127, C-1 remains0; `[C-1]` plus bass1 fails. In C major root-position range48–84, `[G9,14]` succeeds with root127 and its generated B at71 (baseline B59 plus12). In root-position range120–127, `[1@+1]` requests132 and fails with its source offset and no partial result. Test repeated/enharmonic entries and preserving distinct octaves.
- [ ] **3. Build/run TheoryTests to observe the new failures.**
- [ ] **4. Implement resolveProgression.** Iterate steps with previous baseline voices; call existing voiceChord once per baseline. For custom entries, map normalized pitch class to its baseline MIDI pitch, apply relative/exact overrides, and capture the first resolved entry before sorting/deduplication. For ordinary chords, locate the lowest voiced root-pitch-class occurrence, ignoring slash-bass identity. Add bass by rootMidi minus12×offset. Validate every requested pitch before building output; never rely on render-plan clamping. Use original entry or chord offsets in diagnostics. Register ChordResolution.cpp in THEORY_SOURCES.
- [ ] **5. Add a render-plan/MIDI regression.** Feed final pitches into existing buildRenderPlan/MidiFileWriter; assert one on/off per unique pitch, channel1, chosen velocity83, original duration2 beats, metadata conventions intact, and equal-time off-before-on ordering. Build/run TheoryTests; expected0 failures. Commit `feat: resolve custom voicings and root bass`.

### Task 3: Connect Plugin State, Playback, and Preview

**Files:** Modify PluginProcessor.h/.cpp; tests in ProcessorTest.cpp and ChordPreviewChecks.cpp.

**Interfaces:** Append `juce::String formula; juce::String bass;` to LearningRow (empty for ordinary formula/bass Off). Existing notes remains chord-only display text; bass includes the octave-aware note name without the `Bass: ` prefix. No new public playback/preview methods. Consume resolveProgression and ResolvedChord; render plan and preview masks both use `pitches`, learning notes use `chordPitches`.

- [ ] **1. Add failing `checkBassParameterState()` in ProcessorTest.cpp.** Assert appended parameter ID/value/default, four choice labels, automatable status, and unchanged order/defaults of the seven existing parameters. Round-trip bass0–3 and mixed progression text. Restore an old state missing bassOctaves into an instance currently at3: expect0. Repeat with legacy mode-only state and existing nine-scale migration. Test off-message-thread restore follows the existing deferred path.
- [ ] **2. Add failing playback/error tests and preview regressions.** Root-position `[C5,E4,G4]:2` plus bass1 emits `{60,64,67,72}` with velocity83. Learning row separates chord notes and `C4` bass. Invalid bracket or `[C-1]`/bass1 retains last valid live plan/rows, exposes diagnostics, rejects export/drag and stale previews. Empty input clears. In ChordPreviewChecks.cpp verify momentary and held pitches include bass, release all pitches on release/toggle/cancellation, and reject an old displayed revision immediately after bass automation before async dispatch. Reuse the deterministic delayed-plan fixture to ensure a new valid preview survives late plan delivery.
- [ ] **3. Build/run ProcessorTests to observe failures.** Reuse existing playhead, MIDI decoding, message-loop wait helpers and ChordPreviewTestAccess; add checks to existing runners. No sleep-based race tests.
- [ ] **4. Integrate parameter and state.** Append bassOctaves to createParameterLayout, register/unregister listener, and use the existing musical-parameter invalidation path. In setStateInformation insert a missing PARAM with value0 before replaceState, including when restoring into an existing instance. Preserve legacy mode guard and message-thread rebuild path.
- [ ] **5. Integrate shared resolution in rebuildPlan.** After parsing and settings, resolve; on resolution failure set diagnostics and return before replacing plan/rows. Preserve current preview invalidation at rebuild entry. On success build final plan/masks from pitches, rows from chordPitches plus formula/bass metadata, publish cache only for the captured revision. Format custom chord notes with octaves so deliberate register differences are visible; keep ordinary chord notes' existing compact spelling. Do not alter processBlock or exchange ownership for this feature.
- [ ] **6. Verify plugin MIDI-file parity.** Decode processor export and drag files for the same custom/bass fixture and compare pitch/timing/duration/velocity with live and manual output; check tempo/4/4 present only in export. Build/run ProcessorTests, expected0 failures; build ChordText, expected successful VST3. Commit `feat: integrate bass and custom chords in plugin`.

### Task 4: Give the CLI the Same Result

**Files:** Modify CliSettings.h/.cpp, CliOptions.cpp, CliSession.cpp, CliReport.h/.cpp; tests in CliTest.cpp.

**Interfaces:** Append `int bassOctaves = 0;` to CliSettings; add `std::optional<int> parseBassOctaves(std::string_view text)` (off/1/2/3 only). Append optional bassMidi and bassNote to CliChordRow using `std::optional<int>` and `std::optional<std::string>`. Existing buildReport signature stays. Consume Task 2 result and Task 1 voicedNoteName.

- [x] **1. Write failing `checkCliCustomChordsAndBass()` checks.** Parse --bass off/1/2/3; reject0/4/-1/missing values; interactive :bass accepts bass2/off, keeps the old setting after rejection, and appears in help/settings. A custom exact-root chord plus bass reports full pitches `{60,64,67,72}`, bassMidi60/bassNote C4, symbol `C custom`, degree `custom`, source and duration. Cover mixed symbols/numerals/brackets and the nine modes through shared parser and settings tests. Bass-off ordinary fixtures retain their previous note and pitch output.
- [x] **2. Add output and export assertions.** JSON adds settings.bassOctaves and nullable row bassMidi/bassNote without dropping old fields; notes/voicing include final bass, spelledTones excludes the added bass. Human output includes Bass column only when enabled. Invalid resolution has valid=false, diagnostics, no current chords or one-shot MIDI write. Interactive `:export` re-resolves the last valid custom chord after bass changes and refuses an out-of-range pitch. Decoded CLI MIDI pitches match the report voicing.
- [x] **3. Build/run CliTests to observe failures.** Initial compile failed because the new API did not exist yet, as expected.
- [x] **4. Implement setting parsing/options/session/report integration.** Use shared resolution before marking report valid. Use full final pitches in existing note/voicing report fields; retain ordinary note spelling and format custom notes with octaves. Populate bass metadata and truthful custom labels. Ensure :export builds a fresh report of last valid text under current settings and refuses an invalid result before file creation. Preserve existing exit/error conventions and file replacement policy.
- [x] **5. Build/run CliTests and ChordTextCli.** `CliTests` → 308 checks, 0 failures. Human and JSON CLI smoke checks passed for custom chords and bass. Invalid `[C-1]` plus `--bass 1 --out ...` returned exit1 and wrote no MIDI file. Commit `feat: add CLI custom chords and bass options`.

### Task 5: Add the Compact Bass Control and Card Details

**Files:** Modify PluginEditor.h/.cpp; tests in ProcessorTest.cpp and ChordPreviewChecks.cpp.

**Interfaces:** Add bassLabel, bassBox (component ID `bass`), and bassAttachment using the existing ComboBoxAttachment alias, declared after controls. Use APVTS ID bassOctaves with menu item IDs1–4. Extend CardContent with juce::SettableTooltipClient and `mouseMove(const juce::MouseEvent&)` / `mouseExit(const juce::MouseEvent&)` to expose the full hovered card text. Consume LearningRow formula/bass; include both in setRows change detection. Keep current card gestures/revisions intact.

- [x] **1. Add failing layout/control assertions.** At 75/100/125/150%, editor remains720×488/960×650/1200×813/1440×975 (existing scaling checks). Bass menu has four choices and changes the parameter. Summary and menu do not overlap; Loop stays inside its margin. Added checks for long128-entry formula, invalid pitch, custom bass, and multiple pitches.
- [x] **2. Add card display regressions.** Custom title and formula/note/bass data appear in rows. Duplicate bass remains labelled. Hover tooltip contains full formula, chord notes and bass; moving off cards clears it. Existing PREVIEW/HELD padding checks pass at all scales; snapshot review confirms bass card text and badges fit.
- [x] **3. Build/run ProcessorTests to observe failures.** The first run exposed the missing tooltip component and bass control.
- [x] **4. Implement the controls and layout.** Increased the scale-summary row from22 to28 logical pixels. Added the attached bass menu on the right, with a12-pixel gap from the summary. Cards show formula, chord-only notes and a separate bass line. Long text uses ellipses and full text appears in tooltips. A TooltipWindow is owned by the editor. Window size and scaling remain unchanged.
- [x] **5. Build/run ProcessorTests.** `ProcessorTests` → 16853 checks, 0 failures. Captured and inspected valid/invalid custom+bass layouts at75/100/125/150%, plus long formula and preview/held snapshots. DAW drag/export checks remain in Task6. Commit `feat: add compact bass UI and custom chord cards`.

### Task 6: Document and Verify the Whole Feature

**Files:** Modify SYNTAX.md, README.md and approved plan checklist; use existing tests, build wrappers, and .github/pull_request_template.md.

- [x] **1. Update SYNTAX.md.** Document every accepted bracket form, single accidentals, case distinction, duration suffix, first-entry root, baseline-relative offsets, exact-note priority, 128-entry limit, duplicate policy and MIDI errors. State expanded degrees apply inside brackets only. Include `[1,2,4,5]`, `[1@-1,1,8]`, `[1,3,5,F]`, `[F3,1,3,5]`, C minor b3, and pinned E4/G4/C5 root-bass example.
- [x] **2. Update README.md.** Explain Bass Off/1/2/3, per-instance automation/state, same output in every path, CLI --bass/:bass, JSON additions, and manual host checks. Retain current UI scaling and nine-mode instructions.
- [x] **3. Run the full Release build and suites.** The full Release build produced the VST3 bundle and CLI. TheoryTests, ProcessorTests, and CliTests passed with 753, 14938, and 319 checks. The ProcessorTests count varies. The build output showed no project-code warnings. Human and JSON CLI checks passed for custom chords and bass; decoded MIDI pitches matched the report, and invalid bass resolution wrote no MIDI file.
- [x] **4. Review cross-path parity and host limits.** Automated checks cover shared pitches, timing, velocity, plugin playback/preview/hold, export/drag data, and CLI output. Actual DAW play/stop/loop/seek, project save/reopen, MIDI drop acceptance, bass automation, and low/high register listening were not performed. See the manual checklist in README.md.
- [x] **5. Review the whole branch against this plan and approved spec.** The initial read-only review found that enharmonic exact notes could display with the first matching spelling. The resolver now keeps each custom spelling aligned with its resolved MIDI pitch, with theory, plugin-row and CLI regression checks. A second read-only pass confirmed the fix and found no related issue. The full Release build and all suites pass; `git diff --check` is clean, and generated bundles, logs, UI preference files and temporary clips are not tracked.
- [x] **6. Commit documentation and present the result.** README.md, SYNTAX.md, this approved plan, and the design spec are committed in a documentation commit. Report the VST3 build, all three suite totals, warnings, manual host results/omissions, and branch status. PR creation follows a user request; no PR or merge is authorized by plan approval alone.

## Planning Self-Review

- Spec sections2/4/5 → resolver and bounds Task2; root/duplicate fixtures pin exact outcomes.
- Spec section3 → model/parser Task1; automatic/exact/relative placement Task2.
- Spec sections2/5/8 → parameter migration, cancellation and shared output Task3.
- Spec section7 → CLI Task4, including last-valid explicit export.
- Spec section6 → UI Task5, all existing controls and scaled bounds retained.
- Spec section9 → task-specific checks plus full build/manual reporting Task6.
- Interfaces use the same CustomNoteEntry, ResolvedChord and resolver names throughout. No new test framework, scale model, audio exchange, or MIDI writer is introduced.
- Review Focus cases are assigned to explicit task checks. The plan and spec guided implementation; task evidence is recorded above.

## Approval and Execution Record

The user approved the plan and requested implementation in this chat. One implementer completed the sequential tasks, followed by a fresh read-only whole-branch review. See Task 6 for final review, documentation commit, and verification status.
