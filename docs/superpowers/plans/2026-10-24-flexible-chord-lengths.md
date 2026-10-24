# Flexible Chord Lengths Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Preserve the execution method chosen by the user at handoff.

**Goal:** Add a per-instance default length and explicit musical lengths that agree across playback, cards, exports, drag clips, and CLI output.

**Architecture:** A shared JUCE-free length parser resolves retained expressions against a default and time signature before the existing voicing/render-plan pipeline. The audio thread caches host meter using a coherent atomic snapshot; a processor-owned message-thread timer consumes changes, even with no editor. Published plans, learning rows, preview data, and export settings carry the same timing context.

**Tech Stack:** Windows x64, C++17, JUCE 8.0.15, CMake 3.22+, Visual Studio 2022 Build Tools, existing CHECK-based suites.

**Spec:** `docs/superpowers/specs/2026-10-24-flexible-chord-lengths-design.md`, approved on 2026-10-24. Read the entire spec before this plan.

**Status:** Implemented and automatically verified on 2026-10-24 on `codex/flexible-chord-lengths`. Product tasks 1–5 and Task 6's documentation and automated verification are complete; see the [completion record](#completion-record-2026-10-24). Manual DAW checks, an independent final review, and a few enumerated UI regressions remain outstanding and are annotated below.

## Global Constraints

- One internal beat always means one quarter note, including in 6/8. Note fraction N/D resolves to `4 * N / D`; D multiplies by 3/2 and T by 2/3.
- Note denominators: 1, 2, 4, 8, 16, 32, 64. Beat/bar amounts are positive finite numbers. Compact suffixes; D/T and unit words accept case-insensitive aliases.
- Default length: 19 stable choices; new and migrated instances use 4 beats. Explicit bare numeric suffixes remain quarter-note beats.
- Meter numerator: 1–255. Denominator: 1, 2, 4, 8, 16, 32, 64. Missing/invalid host meter uses 4/4; invalid CLI meter is an error.
- One current effective meter resolves the entire progression. No future host meter map is scanned.
- Append APVTS ID `defaultLength`; preserve all existing IDs, ordering, automation values, scale migration, bass migration, and progression text.
- Parsing and music rules stay JUCE-free. Plan rebuilding/publication and file writes stay on the message thread. `AudioPlayHead::getPosition()` stays processBlock-only.
- Preserve triple-buffer ownership and preview cancellation/revision safety. Do not introduce parsing, allocations, locks, or file I/O in the audio callback. Existing host-owned MidiBuffer capacity remains the documented host boundary.
- Follow current host position when adopting new timing. Release old notes before replacement notes; use the revised loop length when Loop is on. Manual preview/hold keeps its gesture-based lifetime.
- Invalid input retains the last valid plugin playback plan/cards and disables export/drag/stale preview. Empty input clears the plan. CLI reports current errors without stale rows.
- Export includes tempo and the effective time signature at tick zero; drag omits both. MIDI resolution remains 960 ticks per quarter. Equal-tick note-offs precede note-ons.
- Logical window: 960 × 650. UI scales: 75%, 100%, 125%, 150%. Default length belongs under settings beside Bass.
- Use the Visual Studio 17 2022 generator with `-A x64`; not Ninja or CTest. Never permanently delete files; use trash-cli for removal.
- This document planned the work; implementation, builds, and product suites began only after plan approval.

## Review Focus

1. Missing signature after 6/8, host stop, bypass, or closed editor: resolve new bars with the latest legally observed context and distinguish actual 4/4 from fallback (Task 2).
2. Meter/default automation racing a rebuild or export: stale work cannot clear a newer request, re-enable stale preview, or pair old note times with new metadata (Tasks 2–3).
3. Loading old state into an instance already using a bar default: restore four beats, rather than retaining the prior instance value (Task 2).
4. Fractions combined with slash-bass/custom syntax, or rounding to the same MIDI tick: preserve token boundaries and release-before-attack ordering without creating stuck notes (Tasks 1 and 3).
5. A short loop crossing more than 64 boundaries in a large host block, and a new one-shot length ending before the playhead: do not silently truncate supported musical timing or leave notes sounding (Task 2).

## File Map and Order

| Files | Responsibility |
| --- | --- |
| New `Source/Theory/Length.h`, `.cpp` | Length/meter types, parser, conversion, presets, display formatting |
| `Source/Theory/Progression.h`, `.cpp` | Timing input, retained length metadata, resolved beat lengths and diagnostics |
| `Source/Theory/MidiFileWriter.h`, `.cpp` | Configurable meter metadata and validated file timing |
| `Source/Plugin/PluginProcessor.h`, `.cpp` | Default parameter, migration, meter capture, processor timer, coherent publication/resync/export |
| `Source/Plugin/PluginEditor.h`, `.cpp` | Grouped length menu, timing status, inherited labels, readable card layout |
| `Source/Plugin/ClipDragComponent.h`, `.cpp` | Meter-aware clip subtitle |
| `Source/Cli/CliSettings.h`, `.cpp`, `CliOptions.cpp`, `CliSession.cpp`, `CliReport.h`, `.cpp` | CLI controls, shared timing, additive output fields |
| `Tests/TheoryTest.cpp`, `Tests/ProcessorTest.cpp`, `Tests/ChordPreviewChecks.cpp`, `Tests/CliTest.cpp` | Meaningful regression checks within the existing runners |
| `CMakeLists.txt` | Add `Length.cpp` to THEORY_SOURCES |
| `SYNTAX.md`, `README.md` | Canonical syntax, settings, time-signature behavior and manual checklist |

Execute Tasks 1–6 in order. CLI Task 4 depends on Tasks 1 and 3, but keep the sequence for one branch. This is one feature because the UI and output paths must share timing rules.

### Execution preflight

- [x] Read repository instructions and the approved spec; inspect clean status on `codex/flexible-chord-lengths`. At implementation time use the worktree skill to decide whether existing isolation is suitable; preserve current work.
- [x] Locate CMake from PATH or the VS2022 Build Tools path in README. In PowerShell, use `$cmakeExe` for that executable. Configure a missing cache with `& $cmakeExe -S . -B build -G 'Visual Studio 17 2022' -A x64`. Reuse the documented JUCE 8.0.15 installation in ignored `libs/JUCE` when present.

## Shared Interfaces and Permanent Values

Task 1 defines these names in namespace `chordtext`, in `Length.h`:

```cpp
enum class LengthUnit { beats, note, bars };
enum class LengthModifier { plain, dotted, triplet };
struct LengthSpec {
    LengthUnit unit = LengthUnit::beats;
    double quantity = 4.0;
    int denominator = 1; // Used only for note fractions.
    LengthModifier modifier = LengthModifier::plain;
};
struct TimeSignature { int numerator = 4; int denominator = 4; };
struct TimingSettings { LengthSpec defaultLength; TimeSignature signature; };
struct LengthParseResult {
    std::optional<LengthSpec> value;
    std::optional<Diagnostic> error;
};
struct LengthPreset {
    std::string_view expression;
    std::string_view displayName;
    LengthSpec length;
};
LengthParseResult parseLength(std::string_view text, std::size_t offset = 0);
std::optional<TimeSignature> parseTimeSignature(std::string_view text);
bool isValidTimeSignature(TimeSignature signature) noexcept;
std::optional<double> resolveLength(const LengthSpec&, TimeSignature) noexcept;
std::string formatLength(const LengthSpec&);
std::string formatBeatCount(double beats);
const std::array<LengthPreset, 19>& lengthPresets();
```

Numeric quantities are validated before conversion. Fraction numerators must parse as positive integers; denominator/modifier combinations must be supported. `resolveLength` returns no value for invalid/non-finite/non-positive results. The formatter uses canonical compact expressions; `formatBeatCount` uses exact small fractions when within tolerance, otherwise a compact decimal. It never changes numeric scheduling values.

The permanent APVTS zero-based mapping is:

| Index | Expression | Index | Expression |
| --- | --- | --- | --- |
| 0 | `1/2` | 10 | `1/16D` |
| 1 | `1/2D` | 11 | `1/16T` |
| 2 | `1/2T` | 12 | `3/16` |
| 3 | `1/4` | 13 | `1beat` |
| 4 | `1/4D` | 14 | `2beats` |
| 5 | `1/4T` | **15** | **`4beats` (default)** |
| 6 | `1/8` | 16 | `1bar` |
| 7 | `1/8D` | 17 | `2bars` |
| 8 | `1/8T` | 18 | `4bars` |
| 9 | `1/16` | | |

Use this mapping for host automation and the menu, with ComboBox IDs equal to index + 1. Group headings are not selectable values. CLI defaults may use any valid expression, including ones absent from this preset list.

### Task 1: Shared length grammar and progression timing

**Files:** Create `Source/Theory/Length.h`, `.cpp`; modify `Source/Theory/Progression.h`, `.cpp`, `CMakeLists.txt`; tests in `Tests/TheoryTest.cpp`.

**Interfaces:** Produce the shared interfaces above. Extend `parseProgression(std::string_view text, const Key& key, const TimingSettings& timing = {})`. Append `LengthSpec length`, `std::string lengthExpression`, and `bool usesDefaultLength = true` to `ProgressionStep`, after existing members so old aggregate initializers retain their meaning. Keep `sourceText` as chord-only text, `offset`, `numericDegree`, and resolved `durationBeats` unchanged in meaning.

- [x] **1. Add `checkLengthParsingAndResolution`.** Assert all 19 presets match the permanent mapping. For straight/dotted/triplet order, expect half `(2, 3, 4/3)` beats; quarter `(1, 1.5, 2/3)`; eighth `(.5, .75, 1/3)`; sixteenth `(.25, .375, 1/6)`. Use real floating-point division and tolerance for thirds. `3/16 == .75`, `1/64T == 1/24`, `1.5beats == 1.5`, `0.5bar` in 7/8 == 1.75. Check lowercase aliases and valid/invalid signature boundaries. Include this assertion pattern:

```cpp
const auto triplet = parseLength("1/8T");
CHECK(triplet.value.has_value());
if (triplet.value)
    CHECK(std::abs(resolveLength(*triplet.value, {6, 8}).value_or(-1.0)
                   - 1.0 / 3.0) < 1e-12);
CHECK(lengthPresets()[15].expression == "4beats");
```
- [x] **2. Run the focused theory build/suite and observe the new checks fail.** `& $cmakeExe --build build --config Release --target TheoryTests --parallel`, then `& .\build\Tests\TheoryTests.exe`. Missing interfaces may fail compilation; after declarations exist the assertions must fail before implementation.
- [x] **3. Implement Length.h/.cpp.** Use locale-independent numeric parsing and shared unit validation; register Length.cpp in THEORY_SOURCES. Keep exact legacy numeric parsing behavior. Canonical forms preserve D versus T and bar versus beat units; do not collapse `1bar` into a numeric beat expression.
- [x] **4. Add `checkProgressionLengthContext`.** With a default of 1/8D in 6/8, parse `C, Dm:2, G:1bar, [1,3,5]:1/8T` and assert lengths `.75, 2, 3, 1/3`, inherited flags `true,false,false,false`, source text unchanged, and original offsets. Check `C/E:1/8`, `[F3,1@-1]:1/4T`, legacy two-argument calls, empty input, and mixed separators. Errors include `1/3`, `1/0`, `0/8`, `1/128`, `1/8DT`, `1beatD`, `1 bar`, `nan`, `inf`, missing/trailing operands. Confirm valid old numeric forms still parse.
- [x] **5. Integrate length parsing into Progression.cpp.** Split the existing colon once, delegate suffix parsing, or use the supplied default when absent. Retain its canonical expression and inheritance. Resolve with the supplied validated signature. Reject non-finite totals or additions that cannot advance the timeline; give a source-local diagnostic rather than overflowing. Do not impose an unrelated minimum on otherwise safe legacy numeric lengths.
- [x] **6. Build/run TheoryTests until all checks pass, then commit** `feat: add shared musical length syntax` with only this task's source, CMake, and test files.

### Task 2: Plugin default state, host meter capture, and live resynchronization

**Files:** Modify `Source/Plugin/PluginProcessor.h`, `.cpp`; tests in `Tests/ProcessorTest.cpp`, `Tests/ChordPreviewChecks.cpp`.

**Interfaces:** Consume Task 1. Append APVTS choice `defaultLength` after `bassOctaves`, default index 15. Add `juce::Timer` privately to the processor and override `void timerCallback()` at 30 Hz; start/stop it with processor lifetime. Add `void requestPlanRebuild() noexcept` for bounded atomic invalidation and `void captureHostTiming(const juce::Optional<juce::AudioPlayHead::PositionInfo>&) noexcept` for audio-thread capture. A processor-owned timer, not an editor timer, consumes meter changes.

Define the following processor-facing type and expose `PublishedTiming getPublishedTiming() const` on the message thread:

```cpp
struct PublishedTiming {
    chordtext::TimingSettings settings;
    bool hostSignatureAvailable = false;
    std::uint64_t requestRevision = 0;
    std::uint64_t hostRevision = 0;
};
```

Append `juce::String lengthExpression` and `bool usesDefaultLength` to LearningRow. Plan slots and the cached export plan retain the timing context/revisions that produced them. Add a test friend `LengthTimingTestAccess` for deterministic deferred-delivery checks, rather than a production debug API.

- [x] **1. Add `checkDefaultLengthState` and `checkHostMeterTiming`.** Assert 19 choices/order/default index 15 and all old parameter positions. Load state lacking defaultLength into an instance already on index 16 and require index 15 plus preserved existing migration. Round-trip selected 1/8D. Extend FakePlayHead to supply optional time signatures; 4/4→6/8→missing gives bar lengths 4→3→4, with availability flags true→true→false. PPQ/BPM without a meter still plays. ProcessBlock must not query the playhead more than the existing once-per-block path.

```cpp
ChordTextAudioProcessor processor;
CHECK(choiceIndex(processor, "defaultLength") == 15);
const auto timing = processor.getPublishedTiming();
CHECK(timing.settings.signature.numerator == 4);
CHECK(timing.settings.signature.denominator == 4);
CHECK(!timing.hostSignatureAvailable);
```
- [x] **2. Run the focused ProcessorTests build/suite and establish failure** with `& $cmakeExe --build build --config Release --target ProcessorTests --parallel`, then `& .\build\Tests\ProcessorTests.exe`.
- [x] **3. Implement state and the meter bridge.** Use one atomic word for a coherent numerator/denominator/availability/revision snapshot, with 4/4 unavailable at construction. Read it on the message thread; do not query the playhead there. `captureHostTiming` handles available, missing, stopped, and no-playhead observations from the normal processBlock. Bypass retains the last observation; the first normal callback after bypass captures changes. Missing fields in an observed callback cause fallback; absence of a callback does not invent a new observation. No audio-thread trigger that posts/allocates is needed: the processor timer polls revisions. Legacy scale synchronization can retain AsyncUpdater while sharing the rebuild consumer.
- [x] **4. Add `checkTimingPublicationRaces`.** With the editor absent, record a meter change and deliver the processor timer callback through LengthTimingTestAccess; verify fresh rows/plan. Interleave default/meter invalidation with a captured rebuild; stale work must not clear pending state or re-enable preview from old cards. Reproduce repeated meter toggles, a queued older AsyncUpdater callback, and state restore while pending. Use existing deterministic mailbox-style hooks instead of thread sleeps.
- [x] **5. Make publication revision-aware.** Rebuild with one captured parameter/default/meter snapshot. Derive pending status from requested versus completed-attempt revisions/context, rather than clearing a boolean at rebuild entry. Keep the completed attempt distinct from the last valid published plan: current invalid input must finish processing and show diagnostics, rather than remain pending forever because its previous valid plan was retained. Stage diagnostics/rows/preview/export data before commit. Recheck requests before committing, retain tags on anything published, and request another pass if invalidated. Guards after publication must still detect a newer request; the final check/store gap must not lose it. Invalidate old preview immediately on a musical request, and publish eligible caches only for the coherent revision. Preserve triple-buffer ownership; do not inspect or keep references to a slot outside its owner lifetime. A tagged older coherent plan may continue as pending; it must never be described or exported as current.
- [x] **6. Add `checkLiveLengthResync`.** With root-position `C:1bar,G:1bar` at PPQ 7, 4/4→3/4 with Loop on must release G and start C at sample 0 after new-plan adoption; Loop off releases and remains silent. Changing the default affects `C,G:2` only on C. Include stop/seek/restart, tempo-only changes, exact boundaries, shared pitches, bass notes, held-preview cancellation, and manual preview lasting beyond a selected sixteenth-note length when no settings change occurs.
- [x] **7. Integrate current-position resync and short-loop safety.** Use the existing new-plan generation scheduler and note-off-first behavior; never reset host PPQ. Add a supported `1/64T` one-step loop in a 48,000-sample block at 48 kHz/240 BPM: all 96 loop attacks must be emitted and releases completed across the boundary, rather than stopping at the current 64-segment cap. Enforce forward progress in loop arithmetic. Do not silently introduce restrictions on safe legacy numeric timing; bring any unavoidable legacy behavior change back for review.
- [x] **8. Build/run ProcessorTests until passing, then commit** `feat: follow default lengths and host meter`.

### Task 3: Coherent export, meter metadata, and file timing safety

**Files:** Modify `Source/Theory/MidiFileWriter.h`, `.cpp`, `Source/Plugin/PluginProcessor.h`, `.cpp`; tests in `Tests/TheoryTest.cpp`, `Tests/ProcessorTest.cpp`, `Tests/ChordPreviewChecks.cpp`.

**Interfaces:** Append `TimeSignature signature {}` to MidiFileSettings, preserving existing aggregate fields/defaults. Keep `serialiseMidiFile(const RenderPlan&, const MidiFileSettings&) -> std::vector<std::uint8_t>` and `writeMidiFile(..., std::string& error) -> bool`. Invalid serialization returns an empty vector; writeMidiFile reports an error before creating/replacing a file. Add message-thread `bool ensureCurrentTimingForExport(juce::String& error)` to the processor. It resolves pending context once and checks it, rather than waiting indefinitely for a quiet host.

- [x] **1. Add `checkMidiLengthAndMeterMetadata`.** Decode writer output and require tick-zero FF 58 data for 3/4 (`03 02 18 08`), 6/8 (`06 03 18 08`), 7/8 (`07 03 18 08`), plus the existing default 4/4 bytes. At PPQ 960, 1/8T=320 ticks, 1/16T=160, 1/2D=2880, and a 6/8 bar=2880. Drag-mode output contains neither tempo nor signature events.

```cpp
MidiFileSettings settings;
settings.signature = {6, 8};
const auto bytes = serialiseMidiFile(RenderPlan{}, settings);
CHECK(containsBytes(bytes, {0xFF, 0x58, 0x04, 0x06, 0x03, 0x18, 0x08}));
```
- [x] **2. Run TheoryTests/ProcessorTests to establish failure**, using each focused target and executable.
- [x] **3. Add validation and implement configured metadata.** Validate signature before encoding its base-two denominator exponent. Validate finite, non-negative representable event times before llround/conversion; MIDI delta VLQs must fit 28 bits and the track chunk length must fit its 32-bit field. Reject file output if a positive note would round to zero ticks; keep parsing/preview eligibility separate from this file-only limitation. Sort by rounded tick, then note-off before note-on, then stable pitch order. Invalid output returns a clear error, never a corrupt or partial file. Default callers keep existing 4/4 output.
- [x] **4. Add `checkPendingTimingExport`.** Change to 6/8 then export before the timer callback: the file must contain new bar note times and 6/8 metadata together. Change default length while invalid text is present: export must fail, not export stale valid rows. Interleave one more context change during the export snapshot check; either serialize a current coherent snapshot or return a pending-timing error without writing. Never call getPosition from export/drag; assert the fake playhead call count is unchanged. A file already written retains its original timing after a later meter change.
- [x] **5. Implement ensureCurrentTimingForExport and snapshot capture.** Use the timing context attached to the resulting export plan for both note times and metadata, including drag resolution. If a newer request supersedes the single refresh attempt, return “Timing is updating. Try again.” before any file write. A later change after snapshot capture does not mutate that file. Keep existing parse-error/empty-export errors, destination policy, and shared writer.
- [x] **6. Add file edge regressions and pass both suites.** A safe legacy numeric length below one MIDI tick still parses, but file export returns a specific resolution error. Overflowing ticks/deltas fail before conversion. Two different beat times rounding to one tick preserve off-before-on ordering. Valid MIDI decoded pitches/times agree with the source plan, including bass. Commit `feat: export coherent musical timing and meter`.

### Task 4: CLI settings, reporting, and interactive export

**Files:** Modify `Source/Cli/CliSettings.h`, `.cpp`, `CliOptions.cpp`, `CliSession.cpp`, `CliReport.h`, `.cpp`; tests in `Tests/CliTest.cpp`. Change CliRun.cpp only if needed to forward a coherent report/file context.

**Interfaces:** Append `LengthSpec defaultLength` and `TimeSignature timeSignature` to CliSettings; add `TimingSettings timingSettings() const`. Existing midiFileSettings() includes the same signature. Consume parseLength/parseTimeSignature, not a second parser. Add `lengthExpression: string` and `usesDefaultLength: bool` to CliChordRow. JSON adds `settings.defaultLength` (canonical string), `settings.timeSignature` (`{numerator,denominator}`), and per-row `lengthExpression`/`usesDefaultLength`; existing numeric fields retain their meanings.

- [x] **1. Add `checkCliLengthOptionsAndReport`.** Parse `--default-length 1/8D --time-signature 6/8`; report `C, Dm:2, G:1bar, [1]:1/8T` with `.75,2,3,1/3` durations and starts `0,.75,2.75,5.75`. Assert settings, source/formula, inheritance, notes, bass, and old JSON fields. Invalid flags/values preserve established usage/error exit codes. The CLI accepts a valid default such as 5/16 absent from plugin presets.

```cpp
CliSettings settings;
settings.timeSignature = {6, 8};
const auto parsedDefault = parseLength("1/8D");
CHECK(parsedDefault.value.has_value());
if (parsedDefault.value) settings.defaultLength = *parsedDefault.value;
const auto report = buildReport("C,G:1bar", settings);
CHECK(report.valid && report.chords.size() == 2);
if (report.chords.size() == 2) {
    CHECK(report.chords[0].durationBeats == .75);
    CHECK(report.chords[0].usesDefaultLength);
    CHECK(report.chords[1].durationBeats == 3.0);
    CHECK(!report.chords[1].usesDefaultLength);
}
```
- [x] **2. Build/run CliTests and establish failure.** Use `& $cmakeExe --build build --config Release --target CliTests ChordTextCli --parallel`, then `& .\build\Tests\CliTests.exe`.
- [x] **3. Implement one-shot and interactive settings.** Add `--default-length`, `--time-signature`, `:default-length`, and `:time-signature`. Use the shared parsers, preserve settings on invalid values, and include choices in help/settings summaries. Pass timingSettings into buildReport's progression parse. Format human rows with requested length plus resolved beats; JSON numeric beats must remain numbers, including thirds.
- [x] **4. Add `checkInteractiveLengthExport`.** Submit valid text, set a new default/signature, submit invalid text, then explicitly export: re-resolve the saved last valid text under current settings. Automatic output for current invalid input writes nothing. Decode the explicit export to assert current lengths/metadata; an unrepresentable tick duration fails without a file. Cover unknown units, case aliases, 1.5beats, 0.5bar, custom single notes, and old no-flag reports.
- [x] **5. Pass CliTests and run a human/JSON smoke example** `chordtext-cli --key C --mode major --default-length 1/8D --time-signature 6/8 "1,4,5:1bar"`; expect durations .75/.75/3 and total 4.5 beats. Commit `feat: expose musical lengths in CLI`.

### Task 5: Compact controls and readable timing state

**Files:** Modify `Source/Plugin/PluginEditor.h`, `.cpp`, `Source/Plugin/ClipDragComponent.h`, `.cpp`; tests in `Tests/ProcessorTest.cpp`, `Tests/ChordPreviewChecks.cpp`.

**Interfaces:** Add editor children with IDs `defaultLengthLabel` and `defaultLengthBox`, plus a ComboBoxAttachment for defaultLength. Append timing data to row comparison/tooltips. Change `ClipDragComponent::setLoopInfo(double beats, bool enabled, chordtext::TimeSignature signature = {})` and store that signature for bar counts. Consume getPublishedTiming for published-card labels; identify pending/new context without falsely labeling old cards as resolved under the new meter.

- [x] **1. Add `checkDefaultLengthLayoutAndCards`.** At all four scales, verify the logical 960×650 size, 19 selectable choices in fixed order, group headings, correct attachment, under-settings placement, no overlap with Bass/scale summary, and Loop inside the right margin. With inherited 1/8D and explicit 1bar in 6/8, row/card details show `1/8D`, `3/4 beat`, DEFAULT only on the inherited card, and `1bar`/3 beats on the explicit card. Changing the default updates only inherited rows. New metadata must invalidate row repaint even if resolved beats happen to be equal.

```cpp
std::unique_ptr<juce::AudioProcessorEditor> editor(createTestEditor(processor));
auto* box = dynamic_cast<juce::ComboBox*>(findEditorChild(*editor, "defaultLengthBox"));
CHECK(box != nullptr);
if (box != nullptr) CHECK(box->getSelectedId() == 16); // Default index 15.
```
- [x] **2. Build/run ProcessorTests to establish failure**, then implement menu construction/attachment and summary-row layout. Keep the chosen placement; do not implement the alternate mockup placement or its preview-only host signature selector.
- [x] **3. Implement card/footer/tooltip text.** Use the shared length and beat formatters. Reserve space for selected length, resolved count, bass, and PREVIEW/HELD badges inside the existing card. Include full metadata in tooltips. Show fallback “4/4 fallback” versus host-provided “4/4”; use current published timing for totals and drag bar counts. Keep equal action heights, piano icon, uppercase drag title and existing font-size relationship.
- [ ] **4. Add state/display regressions.** Exercise empty, valid, invalid, editing, playing, momentary preview, held, export/drag disabled, and unavailable transport. Include 4beats versus 1bar in 4/4 (same numeric length, different label), 7/8 totals, 1/2T, long custom formula, and a bass line at every scale. Capture and inspect representative snapshots; revise layout if text overlaps. A pending timing update cannot appear as a completed current result. **Coverage note:** core states and length labels are covered by `checkLengthControlsAndCardTiming` and the existing preview/editor checks, but the enumerated label cases (4beats vs 1bar, 7/8 totals, 1/2T, long formula, bass at every scale) were not individually asserted. See the completion record.
- [x] **5. Pass ProcessorTests and commit** `feat: add compact default length controls`.

### Task 6: Documentation and final verification

**Files:** Modify `SYNTAX.md`, `README.md`, this plan's completion notes; inspect the whole branch.

**Interfaces:** Document the implemented grammar, permanent default selection, settings, limitations, file semantics, and UI behavior from Tasks 1–5. SYNTAX.md remains the canonical user-facing grammar.

- [x] **1. Update SYNTAX.md and README.** Include all 19 presets, the denominator domain, D/T aliases, compact unit suffixes, decimal beats/bars, custom/single-note examples, 6/8 quarter-note-beat meaning, default inheritance versus explicit numeric beats, dynamic meter/global-context behavior, 4/4 fallback, CLI flags/commands/JSON additions, and export versus drag metadata. Explain that file output may reject a duration smaller than one MIDI tick or outside file timing limits, with no change to safe legacy parsing. Add manual DAW meter/default-automation and stop/seek/loop checks.
- [x] **2. Run the full verification once after final code edits.** From repo root run `cmd /c build.bat test`. Require a successful Release VST3 build and all TheoryTests, ProcessorTests, CliTests passing, with no project-code warnings. Record actual counts/output; do not copy old counts from earlier features. Re-run focused checks after any fixes, and repeat the full build only when those fixes or remaining concerns justify it.
- [x] **3. Run meaningful CLI/file checks.** Verify human and JSON custom/default/meter examples, decoded 6/8 time-signature bytes and triplet ticks, invalid input with no automatic file, and the unchanged legacy `--key A --mode minor "i-iv-V"` example. Keep temporary output inside ignored build/test locations; use trash-cli if cleanup is needed.
- [ ] **4. Check content and scope.** Run `git diff --check`, inspect staged/tracked paths and the full feature diff, and confirm generated bundles, clips, settings, mockups, and local logs are untracked/ignored. Check every spec section against Tasks 1–5 and record any unresolved departure. Perform the execution method's independent final code review and fix actionable findings before claiming completion. **Coverage note:** `git diff --check`, tracked-path review, and the spec-to-task audit were completed (see the completion record); the independent final code review was not performed.
- [ ] **5. Perform available manual host checks.** Record DAW/version, meter/default changes during playback with editor open/closed, old-state restore, preview/hold, export/import, and drag acceptance. Explicitly list checks not performed; automated suites do not establish routing or host drag support. Do not install/copy the VST bundle to a system folder unless that action is authorized. **Coverage note:** not performed; the README manual checklist remains unverified.
- [x] **6. Commit documentation and verification notes** as `docs: explain musical lengths and host meter`. Hand over the branch with observed verification, known limits, and unperformed host checks. Creating a PR follows the user's request and repository template. **Coverage note:** README/SYNTAX documentation was committed in `b2a9a54`; this completion record is committed with the documentation record update.

## Plan Self-Review

- Spec sections 2–5 map to Tasks 1, 2 and 5; host changes in section 6 map to Task 2; UI section 7 to Task 5; output section 8 to Task 3; CLI section 9 to Task 4; compatibility and verification sections 10–11 span Tasks 1–6.
- All five Review Focus conditions have named checks above. No external plan-review agent is required for this self-review.
- Shared LengthSpec/TimingSettings/TimeSignature names, 19 presets/default index 15, APVTS ID, row metadata, CLI keys, and writer settings are consistent across tasks.
- The processor's revision checks must cover the final publication gap, not only a revision read at rebuild start. Completing an invalid attempt is distinct from replacing the last valid plan. Message-thread polling remains alive without an editor; exports use their plan's tagged snapshot.
- No product code, product build, or product suite run was performed to prepare this plan.

## Execution Handoff

Review this plan before implementation. Recommended method: Native execution in this chat, followed by one independent final review, because the six tasks share the timing types and publication rules and benefit from one implementer retaining that context. Subagent-driven execution remains available if the user prefers separate implementation/review gates for each task. Preserve the user's selected method.

## Completion Record (2026-10-24)

**Result:** Product implementation is complete and automatically verified. Manual DAW checks, an independent final review, and a few enumerated UI label regressions remain outstanding and are not claimed.

### Commits

| Commit | Delivered |
| --- | --- |
| `6667653` | Approved design spec |
| `78ab616` | This implementation plan |
| `d1650a7` | Shared length grammar, `Progression` timing, theory tests |
| `406b8de` | `defaultLength` state, host-meter capture, live resynchronization |
| `bb65a67` | Coherent export, meter metadata, file timing validation |
| `b36f62d` | CLI settings, report fields, interactive export |
| `096dda7` | Editor length control, card timing, drag subtitle |
| `b2a9a54` | `SYNTAX.md` and `README.md` documentation |

### Verification

Run on 2026-10-24 from the repository root:

```text
$ ./build.sh --test
924 checks, 0 failures
16796 checks, 0 failures
366 checks, 0 failures
exit 0
```

- Release VST3 rebuilt at `build/ChordText_artefacts/Release/VST3/Chord Text.vst3`.
- A separate `./build.sh` run produced zero `warning C`/`error C` lines from project code.
- `ProcessorTests` count varies with the preview-publication stress check; 16796 is this run.
- `git diff --check` is clean; the tracked tree was clean before this record update; `codex/flexible-chord-lengths` is ahead of `origin/main`.
- CLI spot checks passed: human and JSON output for `--default-length 1/8D --time-signature 6/8`; export contains `FF 58 04 06 03 18 08` at tick zero; `1/3` exits 1 with a source diagnostic; `1/64T` loop safety and pending-meter export are covered by the suites.
- `README.md` test counts were refreshed from 16148 to 16796 for `ProcessorTests`.

### Spec coverage audit

- §2–§4 (decisions, length meaning, grammar) → `Source/Theory/Length.*`, `Progression.*`, `Tests/TheoryTest.cpp`.
- §5 (default/state) → `defaultLength` APVTS at index 15, old-state migration, editor control.
- §6 (host meter) → atomic capture in `processBlock`, processor-owned timer, revision-aware publication, resync, short-loop safety.
- §7 (UI) → grouped menu, card timing text, DEFAULT badge, fallback/host status, drag subtitle.
- §8 (export/drag) → `MidiFileSettings::signature`, coherent export snapshot, drag metadata omission.
- §9 (CLI) → one-shot flags, interactive commands, human/JSON fields.
- §10 (errors/compatibility) → retained last valid plan, disabled export/drag, legacy numeric lengths.
- §11 (verification) → the suites above and this record.

No unresolved spec departure was found. Documentation gaps found during the record audit were fixed in `SYNTAX.md`: legacy exponent-form numbers, the empty-suffix error example, meter-change wording, the CLI signature domain, and lowercase `d`/`t` display.

### Coverage deviations from this plan

- Check names were merged or renamed: `checkHostMeterTiming` is part of `checkDefaultLengthStateAndHostTiming`; `checkTimingPublicationRaces` is distributed across that check, `checkPendingTimingExport`, `checkLiveLengthResynchronization`, and the preview-revision checks; `checkLiveLengthResync` is `checkLiveLengthResynchronization`; `checkDefaultLengthLayoutAndCards` is `checkLengthControlsAndCardTiming`; CLI checks are inline in `Tests/CliTest.cpp` `main`.
- Task 2.4/2.6 scenarios are covered across those checks plus the existing scheduler and preview tests. Repeated meter observations (4/4 → 6/8 → missing), pending export, stale-card rejection, and state restore with a pending scale sync are asserted; stop/seek/tempo and preview cancellation retain their pre-existing coverage rather than new length-specific assertions.
- Task 5.1 verifies menu contents, attachment, under-settings placement, Bass overlap, and tooltips at the default scale. Four-scale bounds and Loop-margin behavior come from the existing UI-scaling tests; menu section headings are not separately asserted.
- Task 5.4's enumerated label cases were not individually asserted; see the annotation on that item.
- Red-state steps (1.2, 2.2, 3.2, 4.2, 5.2) belonged to the implementation session; their original failing output is not preserved in the repository.

### Not performed

- Independent final code review (Task 6.4).
- Manual DAW verification (Task 6.5): routing, live meter/default changes with the editor open and closed, host-state restore, preview/hold, export/import, drag acceptance, and mixed-DPI behavior.
- Native drag acceptance and live host-meter behavior cannot be established by the automated suites.

### Record maintenance

- `README.md` and `SYNTAX.md` documentation: `b2a9a54`, with the audit fixes above.
- This completion record and the spec status update are committed as the documentation record update.
