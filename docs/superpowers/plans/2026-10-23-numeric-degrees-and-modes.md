# Numeric Degrees and Nine Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Status:** Implemented on `codex/numeric-degrees-and-modes`. Full VST3 build passed. Theory: 531 checks; processor/UI: 409; CLI: 262. All had zero failures. Independent review found no remaining issues. Native DAW checks remain unverified. No commit or PR created.

**Goal:** Accept scale-derived numeric triads and sevenths and expose nine scales consistently in Chordscript and its CLI, while preserving existing projects and automation.

**Architecture:** Define scale metadata in the JUCE-free Key module and introduce a dedicated numeric parser that resolves into existing ChordSpec objects. Append a new scaleMode parameter while retaining the legacy two-choice mode parameter. Extend presentation data with numeric degrees and reuse existing rendering, voicing, MIDI, and CLI report paths.

**Tech Stack:** C++17, JUCE 8.0.15, Windows x64, Visual Studio 2022 MSBuild/CMake, existing CHECK-based theory/processor/CLI test executables.

**Spec:** [Approved design](../specs/2026-10-23-numeric-degrees-and-modes-design.md).

## Global Constraints

- Nine choices, in order: Major (Ionian), Minor (Aeolian), Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor, Melodic Minor.
- Minor is natural minor; Melodic Minor always uses ascending intervals.
- Numeric syntax: 1-7, degree(7), finite explicit triad/seventh quality overrides, and existing :duration suffixes.
- Reject joined numbers, numeric root accidentals, numeric slash bass, general numeric alterations, and scale-derived ninths.
- Preserve existing Roman quality/case rules and absolute chord pitches.
- Preserve the compact 960 x 650 carbon/teal editor, action heights, and Loop right margin.
- Preserve legacy mode ID, choice count, normalization, and Major/Minor values; append scaleMode after existing parameters.
- Do not add dependencies or change plugin identity, existing parameter order, voicing algorithms, or render scheduling.
- Never permanently delete files; use trash-cli if a file must be removed.
- Product implementation begins after plan review and execution-method selection. Do not publish a PR in this work without a user request.

## Review Focus

- Unsupported digit strings such as 17 and 1645 must fail rather than becoming an extension or partial progression (Task 2).
- Changing Key/Mode with invalid input must preserve prior card analysis and notes while updating the current scale summary (Tasks 3 and 5).
- Loading old Minor state into an instance previously set to an extended mode must resolve to Minor, with no stale deferred event overriding restore (Task 3).
- Legacy and new parameter events in opposite orders must converge to the last external request without synchronization loops (Task 3).
- Extended-scale spelling and augmented major sevenths must survive voice leading, display, CLI JSON, and MIDI export without silently becoming ordinary major chords (Tasks 1, 2, 4, 5, and 6).

## Files and Responsibilities

- Modify Source/Theory/Key.h and Key.cpp: scale metadata, mode names/aliases, spelled scale notes, and scale-degree roots.
- Create Source/Theory/NumericDegreeParser.h and NumericDegreeParser.cpp: strict numeric grammar and scale-derived quality resolution.
- Modify Source/Theory/Progression.h and Progression.cpp: numeric dispatch and optional numeric-source metadata.
- Modify CMakeLists.txt: add NumericDegreeParser.cpp to THEORY_SOURCES, used by all four consumers.
- Modify Source/Plugin/PluginProcessor.h and PluginProcessor.cpp: scale parameter, migration/synchronization, scale summary, and numeric card metadata.
- Modify Source/Plugin/PluginEditor.h and PluginEditor.cpp: nine-choice selector, summary line, numeric analysis layout, and updated input help.
- Modify Source/Cli/CliSettings.cpp, CliOptions.cpp, and CliSession.cpp: shared scale parsing, names, and help. Keep existing CLI report schema.
- Modify Tests/TheoryTest.cpp, Tests/ProcessorTest.cpp, and Tests/CliTest.cpp: musical, compatibility, output, and presentation regression coverage.
- Modify README.md and SYNTAX.md: user-visible feature rules and current editor controls.
- Reuse Chord.cpp's existing maj7 plus #5 alteration for augmented major sevenths; do not introduce an arbitrary-interval chord model.

## Execution Setup

- [x] Read this plan and the approved design. Record the execution choice and inspect git status before changes.
- [x] Use superpowers:using-git-worktrees at execution time. Preserve these two documents when establishing the feature checkout; prefer the writable repository and existing JUCE dependency over a checkout that cannot build.
- [x] Use branch codex/numeric-degrees-and-modes, or reuse an already established suitable feature branch. Do not overwrite unrelated changes.
- [x] Locate CMake using Get-Command cmake or the Visual Studio Build Tools path in build.bat. In the commands below, $cmakeExe denotes that executable's absolute path. Use VS 2022 x64; retain the existing build directory where suitable.
- [x] Inspect the existing baseline results and changed files. Run a baseline only if tree changes or uncertainty invalidate the recorded successful run. Request sandbox escalation for a build if needed; prior runs stalled at Checking File Globs inside the sandbox.

## Task 1: Shared Scale Definitions

**Files:** Key.h, Key.cpp, Tests/TheoryTest.cpp.

**Interfaces:**
- Keep Mode::major = 0 and Mode::minor = 1; append dorian, phrygian, lydian, mixolydian, locrian, harmonicMinor, melodicMinor.
- Expose const std::array<Mode, 9>& allModes(); std::string_view modeName(Mode); std::string_view modeDisplayName(Mode); std::optional<Mode> parseModeName(std::string_view).
- Expose const std::array<int, 7>& modeIntervals(Mode).
- Add std::array<PitchClass, 7> Key::scaleNotes() const. Keep Key::degree(int) restricted to 1-7; callers wrap indices before calling.
- Key::name() keeps old C major / A minor spelling; extended modes use readable lowercase names with spaces. modeName returns the design's canonical CLI tokens.

- [x] Add scale fixture assertions for all nine interval rows from the spec. Assert old enum values, canonical names, case-insensitive aliases, and exact scale-note spelling. Representative assertions:

```cpp
CHECK (static_cast<int> (Mode::minor) == 1);
CHECK (parseModeName ("DORIAN") == std::optional<Mode> { Mode::dorian });
CHECK ((Key {{'C', 0}, Mode::dorian}.degree (3).name() == "Eb"));
CHECK ((Key {{'A', 0}, Mode::melodicMinor}.degree (6).name() == "F#"));
CHECK ((Key {{'G', 1}, Mode::major}.degree (7).name() == "F##"));
```

- [x] Build TheoryTests using `& $cmakeExe --build build --config Release --target TheoryTests`; confirm the new assertions fail to compile or fail for the missing modes.
- [x] Implement the declared metadata and Key operations from one scale table, with the exact intervals and names in the spec. Handle invalid internal enum values defensively by falling back to Major; external unknown names return nullopt.
- [x] Run the targeted build and `build/Tests/TheoryTests.exe`; require exit 0 and zero failures, including all prior Major/Minor cases.

## Task 2: Numeric Chord Resolution and Progression Integration

**Files:** new NumericDegreeParser.h/.cpp; Progression.h/.cpp; CMakeLists.txt; Tests/TheoryTest.cpp.

**Interfaces:**
- NumericDegreeParseResult contains std::optional<ChordSpec> chord, std::optional<Diagnostic> error, and int degree = 0.
- NumericDegreeParseResult parseNumericDegree(std::string_view token, const Key& key, std::size_t offset).
- Append int numericDegree = 0 to ProgressionStep; existing four-field aggregate initializers remain valid.
- parseProgression continues to return ProgressionParseResult; it records the numeric degree when the numeric parser succeeds.

- [x] Add valid/invalid numeric parsing assertions and integration tests. In C Major, require 2 -> Dm; 2(7) -> Dm7; 2maj -> D; 2maj7 -> Dmaj7; 2dom7 -> D7. In A Harmonic Minor require 1(7) -> AmMaj7 and 3(7) -> Cmaj7#5, with exact tones A C E G# and C E G# B.
- [x] Add independent expected symbol fixtures for seven triads and seven sevenths in every scale. For the first seven scales, use C/D/E/F/G/A/B as respective tonics in mode order Major/Dorian/Phrygian/Lydian/Mixolydian/Minor/Locrian: expect rotations of C Dm Em F G Am Bdim and Cmaj7 Dm7 Em7 Fmaj7 G7 Am7 Bm7b5. Use separate literals for A Harmonic Minor (Am Bdim Caug Dm E F G#dim; AmMaj7 Bm7b5 Cmaj7#5 Dm7 E7 Fmaj7 G#dim7) and A Melodic Minor (Am Bm Caug D E F#dim G#dim; AmMaj7 Bm7 Cmaj7#5 D7 E7 F#m7b5 G#m7b5). Verify representative exact tones and compare all results with independently stated expected chord symbols, rather than invoking the numeric algorithm to generate expectations.
- [x] Pin mixed and timed input: `1:8, vi7, F, 5(7)` is valid with durations 8,4,4,4; `1-6-4-5` has four steps. In C Dorian `3, III, iii` resolves Eb,Eb,Ebm. Empty input still succeeds.
- [x] Pin rejected forms 0,8,9,17,27,57,01,1645,b7,#4,2(9),2maj(7),2(7)m,2/, and unmatched parentheses. For `C, 1645`, assert diagnostic token 1645 and offset 3. For malformed durations retain the existing duration error. No partial valid plan on errors. Retain absolute/Roman ninth tests.
- [x] Run the TheoryTests build/test to observe the new failures before implementation.
- [x] Implement strict whole-token parsing. A bare degree stacks scale offsets 0,2,4; `(7)` adds offset 6. Include octave offsets when deriving root-relative intervals. Match major/minor/diminished/augmented triads and seventh combinations to existing qualities; map augmented major seventh to maj7 with alteration {5,1}. For explicit overrides, consume the exact finite vocabulary in the approved spec; never accept a bare numeric suffix.
- [x] Dispatch digit-leading input and accidentals followed by digits to the numeric parser so unsupported numeric roots get a useful diagnostic. Keep other dispatch behavior and original token offsets. Register the new source in THEORY_SOURCES and populate numericDegree only after successful numeric parsing.
- [x] Rebuild/run TheoryTests; require zero failures. Add one render-plan equivalence check comparing event fields and length for numeric vs explicit progressions, under root-position and voice-led settings, including a seventh and a duration.

## Task 3: Processor Scale Selection and Saved-State Compatibility

**Files:** PluginProcessor.h/.cpp; Tests/ProcessorTest.cpp.

**Interfaces:**
- Append AudioParameterChoice ID {"scaleMode",1} after loop, using allModes order and modeDisplayName labels. Keep legacy ID {"mode",1} and its exact two labels Major/Minor.
- Add juce::String getScaleSummary() const, reflecting the current tonic and effective scale, e.g. C Dorian · C D Eb F G A Bb.
- Append int numericDegree = 0 to LearningRow and populate it from ProgressionStep; leave row.degree as Roman analysis.
- selectedKey() resolves the effective nine-choice selection. Numeric and Roman parsing share this Key.

- [x] Add regression assertions for legacy mode's two choices/normalization, scaleMode's nine choices, unchanged old parameter order, all nine effective selections, numeric row metadata, and the example summary.
- [x] Add state tests: old state with no scaleMode and mode=1 restores Minor into a previously Dorian instance; new state with scaleMode=Harmonic Minor and legacy mode=Major restores Harmonic Minor. Flush asynchronous messages before and after restore to detect stale selection requests. Round-trip progression and all existing parameter values.
- [x] Add ordered-event tests: legacy Minor then Dorian ends Dorian; Dorian then legacy Minor ends Minor; selecting Major/Minor via scaleMode synchronizes legacy mode once without causing an event loop; repeated extended selections are stable. Exercise parameter changes from a non-message thread with bounded joins and dispatch on the message thread.
- [x] Build ProcessorTests with `& $cmakeExe --build build --config Release --target ProcessorTests` and run `build/Tests/ProcessorTests.exe`; observe new failures.
- [x] Implement deferred synchronization: parameter callbacks record the latest external scale request atomically and trigger the existing async updater; do not modify ValueTrees or build plans on an audio callback. Distinguish internal synchronization callbacks from external events with a reentrancy guard. On the message thread apply the latest requested scale, update the corresponding parameters, then rebuild once. Preserve observed external request ordering and allow a newer request during a rebuild to schedule another pass.
- [x] Before replaceState, migrate a copied legacy ValueTree by inserting scaleMode from the saved mode value. During restore suppress internal synchronization events, discard obsolete pending requests, then initialize effective selection from the migrated tree and rebuild through the existing thread-aware restore path. For new state scaleMode has precedence. Do not reinterpret a normalized legacy Minor value as the last of nine choices.
- [x] Populate new metadata and summary from shared theory definitions. On invalid text keep last-valid rows, their old degree analysis, and the existing render plan. Current settings still drive the summary.
- [x] Run ProcessorTests; require zero failures, including existing transport/note lifecycle/export tests. Escalate for the test's Documents clip writes if necessary.

## Task 4: CLI and REPL Parity

**Files:** CliSettings.cpp, CliOptions.cpp, CliSession.cpp; Tests/CliTest.cpp. Use existing CliReport.cpp behavior unless a test exposes a required correction.

**Interfaces:**
- Existing cli::parseMode delegates to parseModeName; CliSettings::modeName returns chordtext::modeName.
- Existing buildReport and formatJson signatures/schema remain stable. Existing source field preserves numeric input, symbol is resolved, degree is Roman analysis, and settings.mode is canonical.

- [x] Replace tests that specifically reject dorian with rejection of a genuinely unknown scale. Add --mode and :mode coverage for all nine canonical names, case variations, ionian/aeolian aliases, and unknown values.
- [x] Assert a Dorian report preserves source `3(7)`, resolves the correct Ebmaj7, and returns canonical mode dorian. Assert harmonic-minor JSON contains the correct tones and symbol Cmaj7#5 for `3(7)`. Retain unchanged default major/minor settings output. Numeric parse errors keep the CLI's existing error exit conventions.
- [x] Build/run CliTests to observe failures, then route mode name parsing/formatting to the shared theory helpers. Update option and REPL help to enumerate nine choices and demonstrate `1, 6, 4, 5` and `2(7)`.
- [x] Build/run CliTests and ChordTextCli; require zero test failures and successful one-shot/JSON output for `--key A --mode harmonic-minor "1(7), 3(7), 5dom7"`.

## Task 5: Compact Editor Scale Summary and Numeric Cards

**Files:** PluginEditor.h/.cpp; Tests/ProcessorTest.cpp.

**Interfaces:**
- Bind modeBox and modeAttachment to scaleMode. Give the selector component ID modeBox and the summary label ID scaleSummary.
- Existing sequence cards consume LearningRow::numericDegree. If nonzero, show `Degree 2 · ii7` for a Dm7 card; the chord title remains Dm7. Legacy notation retains its existing Roman analysis.
- Expose summary text through a juce::Label populated by processor.getScaleSummary(); do not duplicate scale tables in the editor.

- [x] Add editor assertions: nine-choice mode selector, mode changes update summary and resolved numeric cards, and summary remains correct after reopening an editor. Compare numerical LearningRows with equivalent explicit input.
- [x] Test invalid settings changes: after valid C Major `2(7)`, set invalid text then Dorian; summary updates to C Dorian while retained cards remain Dm7 with prior analysis, dimmed as Last valid preview. Editing and empty states retain the approved action availability.
- [x] Check summary/selector bounds fit the existing window, Loop remains within the right margin, drag/export/reveal heights match, and inline diagnostics do not intersect the summary. Check the longest mode label and sharp-heavy scale text fit their allotted area.
- [x] Build/run ProcessorTests to observe the new missing UI behavior, then implement the binding, summary refresh, updated empty help, and numeric analysis layout. Use two lines or allocate the full analysis row; do not squeeze `Degree 2 · ii7` into the current 52px Roman badge. Include numericDegree in card change detection.
- [x] Run ProcessorTests with zero failures. Inspect an actual editor render if a supported host or render harness is available; otherwise explicitly record the remaining visual host check rather than claiming visual parity.

## Task 6: Documentation and Integrated Verification

**Files:** README.md, SYNTAX.md; relevant test files only if integration exposes gaps.

- [x] Document the nine scales, ascending Melodic Minor rule, numeric grammar and override vocabulary, mixed notation, duration examples, rejected joined numbers, and differing numeric-vs-Roman quality behavior. Include `5` vs `V` in A Minor and `1(7)` in A Harmonic Minor.
- [x] Update existing controls/editor descriptions where needed for scaleMode, numeric cards, scale summary, and the current Chordscript labels. Explain that the new selector's host parameter is Scale Mode and the old Mode parameter continues to control Major/Minor for compatibility.
- [x] Run `build.bat test` from the feature checkout. Require exit 0, a built VST3, and zero failures in TheoryTests, ProcessorTests, and CliTests. Do not repeatedly run the full suite after success unless new changes justify it.
- [x] Compare exported/drag MIDI event read-back for representative numeric and explicit equivalents: C Major sevenths, A Harmonic Minor augmented-major seventh, and C Dorian mixed input. Verify pitches, lengths, velocity, and note-off-before-note-on ordering with the existing helpers; add targeted coverage only where earlier tasks do not prove it.
- [x] Run `git diff --check` and inspect `git status --short --branch` and the full diff. Confirm only approved feature files changed and no temporary clips or logs are tracked.
- [x] Request a final code review using the execution method's review workflow. Address actionable findings and rerun only affected checks plus any newly necessary integration checks.
- [x] Report automated verification separately from manual DAW routing, normalized host automation, and native drag acceptance. Keep branch/checkout for user review. Create commits or a PR only within the user's authorized scope; never merge automatically.

## Self-Review Record

- Spec coverage: scale table/names -> Task 1; grammar/musical cases -> Task 2; compatibility/processor -> Task 3; CLI parity -> Task 4; editor/learning -> Task 5; docs/MIDI/build -> Task 6.
- Interfaces use one shared Mode enum and metadata source. ProgressionStep.numericDegree feeds LearningRow.numericDegree; CLI preserves its existing source/symbol/degree schema.
- All five Review Focus cases are assigned concrete assertions above. No independent feature, ninth extension, new modal family, or rhythm/voicing behavior is added.
- At plan creation, no product code, tests, dependencies, or Git branch had been changed. Implementation is now complete; see the status above.
