# Chord Card Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking. A separate implementation agent will execute the complete plan after user approval; the planning agent remains the coordinator.

**Goal:** Audition one chord by left-press or right-click hold, with safe MIDI release and return to the DAW's current progression position.

**Architecture:** Cache the existing voicer's pitches on the message thread. Publish fixed-size desired preview requests through a separate single-producer/single-consumer triple buffer. Use atomic cancellation and data revisions to invalidate old requests from any relevant callback. The audio thread owns all note emission and gives manual preview exclusive priority over the existing scheduler.

**Tech Stack:** C++17, JUCE 8.0.15, Windows x64, Visual Studio 2022/CMake, existing CHECK test suites.

**Spec:** [Approved chord preview design](../specs/2026-10-24-chord-preview-design.md).

**Status:** approved by the user on 2026-10-24. Implementation is authorized for a separate agent under this plan. No commit, push, PR, merge, or installation is authorized.

## Global Constraints

- Left-down starts a momentary preview; matching left-up stops it. Right-down toggles a hold. One manual chord per instance.
- Manual preview works without transport, PPQ, or tempo while the host processes the plugin. Chordscript remains a MIDI generator with no synth.
- Preview overrides progression output. Release resumes at current PPQ, without restarting the DAW or progression.
- Latest desired state wins per processing block. Completed gestures must not replay late. A new gesture on the same sounding card retriggers it.
- The original voicer determines pitches. Use channel 1 and the current velocity. Card beat length does not limit preview.
- Cancel on text edits before debounce, musical parameter changes, state restore, editor close, bypass, and processing reset. Do not cancel on user zoom or ordinary focus loss of a right hold.
- Preview persists across transport start/stop/seek/tempo changes. Missing transport metadata affects normal scheduling only.
- Keep preview out of APVTS, project state, UI preference storage, MIDI files, and CLI behavior. Preserve all current parameter IDs and migration.
- Preserve render-plan triple-buffer ownership and message-thread plan builds. Do not parse, rebuild, allocate plugin-owned memory, lock, or touch GUI/files in processBlock.
- Follow AGENTS.md. Use VS 2022 x64, not Ninja. Never use permanent deletion commands; use trash-cli if removal is required.
- No product changes before plan approval. No push, PR, merge, or install is authorized by plan approval. Preserve unrelated work.

## Review Focus

- Rapid gestures and delayed mouse-up must not revive a completed preview or silence a newer hold (Tasks 1, 2, 4).
- Automation, bypass, and reset must invalidate a queued request even if no new UI request can be published (Tasks 1 and 3).
- Missing/stopped transport must not release an active manual chord on every block (Tasks 2 and 3).
- Returning mid-chord or at an exact boundary must emit the right chord once, with all old note-offs first (Task 2).
- Zoomed/scrolled hit testing, release outside the card, and stale cards must not select the wrong chord or leave a hold active (Task 4).

## Files and Responsibilities

- Create `Source/Plugin/ChordPreview.h` and `.cpp`: fixed request/feedback types and two SPSC triple-buffer exchanges. No theory interpretation or MIDI scheduling here.
- Modify `Source/Plugin/PluginProcessor.h` and `.cpp`: cache voicings, validate gestures, publish requests, invalidate from callbacks, arbitrate output, and report applied preview feedback.
- Modify `Source/Plugin/PluginEditor.h` and `.cpp`: card hit testing, pointer gestures, pending-input notification, feedback display, and cleanup.
- Create `Tests/ChordPreviewChecks.h` and `.cpp`: feature checks called from ProcessorTests. Keep this feature's checks out of the already large ProcessorTest.cpp body.
- Modify `Tests/ProcessorTest.cpp` only to include the new header and call `runChordPreviewChecks()` after GUI initialization. Existing regression checks stay intact.
- Modify `CMakeLists.txt`: add ChordPreview.cpp to PLUGIN_SOURCES and ChordPreviewChecks.cpp to ProcessorTests sources. No new test executable.
- Modify `README.md`: explain gestures, routing, override/resume, cancellation, and stopped-host processing limits. SYNTAX.md needs no grammar change.

## Execution Setup and Handoff

- [x] Read both approved documents and AGENTS.md. Inspect live Git status and recent commits; planning started on main at 39496e6 after PR #12. Do not assume that checkout is still current or clean.
- [x] Use the using-git-worktrees skill to establish an isolated feature branch, preferably `codex/chord-preview`. Reuse the writable checkout and installed JUCE/build dependencies if a managed worktree falls outside writable roots or cannot build. Preserve the two planning documents and unrelated concurrent work.
- [x] Keep an execution ledger under ignored `.superpowers/sdd/2026-10-24-chord-preview/`. Record task evidence and design changes. Use PowerShell instead of Bash helpers where appropriate.
- [x] Locate CMake with Get-Command or the Visual Studio Build Tools path in build.bat. Below, `$cmakeExe` denotes that executable. Build commands may need the established sandbox escalation.
- [x] Inspect current build evidence before choosing a baseline. Changes in PR #12 may have changed build wrappers; read them. Do not repeat a full successful build without a new change or unresolved concern.
- [x] Implement tasks sequentially with focused checks. For this handoff, keep changes uncommitted until the final reviewed result unless the user separately authorizes commits. Do not push, create a PR, install the bundle, or merge.

## Task 1: Bounded Request and Feedback Exchange

**Files:** ChordPreview.h/.cpp; CMakeLists.txt; ChordPreviewChecks.h/.cpp; ProcessorTest.cpp.

**Interfaces:** namespace `chordscript`:

- `enum class PreviewMode { idle, momentary, held };`
- `using PreviewGestureId = std::uint64_t;` Zero means no accepted gesture.
- `struct PreviewRequest`: `PreviewMode mode = idle; int stepIndex = -1; PreviewGestureId gestureId = 0; std::uint64_t cancellationEpoch = 0; std::uint64_t dataRevision = 0; std::array<std::uint64_t, 2> pitches {}; int velocity = 100;` The two masks cover pitches 0–127.
- `struct PreviewFeedback`: the same mode, stepIndex, gestureId, cancellationEpoch, and dataRevision fields, without pitch masks or velocity.
- `class ChordPreviewExchange`: `void publishRequest(const PreviewRequest&) noexcept; bool readLatestRequest(PreviewRequest&) noexcept; void publishFeedback(const PreviewFeedback&) noexcept; bool readLatestFeedback(PreviewFeedback&) noexcept;` Each read returns false if there is no fresh value and leaves the caller's cached state intact.
- Requests have one writer (message) and one reader (audio). Feedback has one writer (audio) and one reader (message). Fixed payloads own their data; no pointers into vectors or GUI objects.
- New test entry point: `void runChordPreviewChecks();` declared in ChordPreviewChecks.h, defined in its .cpp.

- [x] Add `checkPreviewExchange()` called by runChordPreviewChecks. Assert initial reads are false; publishing request A then B before a read returns B; a second read without a publish returns false; feedback has equivalent behavior; all fields, including both pitch masks and 64-bit identity fields, survive round-trip.
- [x] Run focused ProcessorTests build and suite; observe the new missing interface failure. Record the actual failure, not an assumed red result.
- [x] Implement the two exchanges using independent three-slot fixed arrays and an atomic index/fresh-bit exchange, matching the existing plan-buffer ownership pattern. Writer fills only its private slot; reader consumes only its private slot. Use acquire/release ownership transfer. Do not use a seqlock over ordinary non-atomic payload memory.
- [x] Add a bounded two-thread exchange stress check. One producer publishes correlated identity/mask fields; one consumer checks every received payload for consistency and eventually receives the final publication. Coalesced intermediate publications are allowed. Never check shared payloads before consuming ownership.
- [x] Add static assertions for trivially copyable payloads and lock-free index/64-bit atomics on the Windows x64 target.
- [x] Add sources and run `& $cmakeExe --build build --config Release --target ProcessorTests --parallel` followed by `./build/Tests/ProcessorTests.exe`. Require zero failures.

## Task 2: Processor Preview and Output Priority

**Files:** PluginProcessor.h/.cpp; ChordPreviewChecks.cpp.

**Interfaces:** add message-thread public methods to ChordTextAudioProcessor:

- `chordscript::PreviewGestureId beginMomentaryPreview(int stepIndex, std::uint64_t displayedRevision);`
- `void endMomentaryPreview(chordscript::PreviewGestureId gestureId);`
- `void toggleHeldPreview(int stepIndex, std::uint64_t displayedRevision);`
- `chordscript::PreviewFeedback getPreviewFeedback();`
- `std::uint64_t getPreviewCardRevision() const noexcept;` Message thread: return the cached displayed-row revision when eligible, or 0 for stale/invalid/pending data.
- `void cancelChordPreview() noexcept;` Any-thread invalidation only: increment an atomic cancellation epoch. It must not publish into the SPSC mailbox or touch message-owned state.
- `void setPreviewInputPending(bool pending);` Message thread; entering pending cancels immediately and blocks new requests until cleared.

Private state: one ChordPreviewExchange; message-owned cached voicings/current velocity, cache revision, pending flag, next gesture ID, and desired request; audio-owned current request/applied identity; cached feedback on the message thread. Use separate atomic `previewCancellationEpoch` and `previewDataRevision`. Keep names consistent throughout tasks.

- [x] Add `checkMomentaryAndHeldPreview()` with a local configurable test playhead and no-playhead fixtures. The existing FakePlayHead is defined in another translation unit; define the new fixture locally in ChordPreviewChecks.cpp. For C major root-position, the existing C3–C6 range (index 1, low MIDI 48), velocity 83, assert left-down produces exactly `{48,52,55}` at channel 1/velocity 83/sample 0; the next block produces no new events; matching left-up produces three note-offs. Repeat for right hold/toggle.
- [x] Add `checkPreviewReplacementAndCoalescing()`: C→Dm produces three old note-offs before new note-ons; new C gesture retriggers C even with identical pitches; momentary replaces held with no restoration; stale momentary-up cannot cancel a newer hold. Down/up before a block is silent; C→Dm before a block emits only Dm. Check shared pitches and invalid card indexes.
- [x] Add `checkPreviewPlaybackPriority()`: start normal C, hold another card, advance through several chord boundaries while held, then release at PPQ 5.0 in `C, Dm, G` and assert only Dm resumes. Release at PPQ 4.0 and assert exactly three Dm note-ons, not six. Assert old note-offs precede every replacement/resume note-on.
- [x] Extend that check with 48 kHz, 120 BPM, 480-sample blocks and release at PPQ 3.99. Assert C resumes at sample 0, C note-offs precede Dm note-ons at sample 240, and no event restarts the progression at beat 0.
- [x] Build/run the focused suite and record the missing behavior failures.
- [x] Cache the original voicings and velocity during successful rebuildPlan. On rebuild entry, invalidate preview eligibility and advance data revision; parse failures preserve normal last-valid rows/plan but leave preview ineligible. Only publish/cache pitches for the revision captured at rebuild start; if an external change advances it during rebuild, new requests must reject that stale cache until the next coherent rebuild.
- [x] Implement the public gesture methods. Request only from a valid, non-empty, non-pending cache whose revision is current and matches displayedRevision. Revision 0 is never eligible. Reject a card from an older rendered revision even if its index is still in range. Copy the selected voicing into pitch masks; each newly started gesture gets a new ID. Right toggling uses message-owned desired state filtered by current epoch/revision, so two right-clicks before audio processing still cancel. endMomentaryPreview changes state only for the matching current momentary gesture.
- [x] Publish request data with the current cancellation epoch and cache revision. An idle request releases the currently applied preview; an old-epoch or old-revision request is treated as idle. Cancellation is independent of mailbox availability. Begin methods return 0 when rejected.
- [x] In processBlock, emit pending releases, consume the render plan as usual, consume preview request once, validate epoch/revision, and read host transport once for status/tempo caching. Split normal-transport validation from note release so missing/stopped transport does not silence preview.
- [x] Arbitrate output exclusively. Reuse activePitches for the sole sounding source and preserve MIDI event ordering. Entering/replacing preview releases old output, starts the requested pitches at sample 0, and sets normal scheduler resynchronization pending. Continued preview emits no new notes and skips progression scheduling. getCurrentStep reports no normal sounding step while overridden; preview feedback identifies the manual card.
- [x] Ending preview releases its notes, clears the normal scheduler's hasExpectedBeat, and executes the existing resynchronization/scheduling path using that block's current PPQ. Retain findActiveStep's exact-boundary guard. Publish applied feedback after arbitration; feedback validates epoch/revision when read by the message thread.
- [x] Run focused ProcessorTests; require zero failures, including all existing scheduler checks. Keep musical parsing, voicing, render-plan creation, export, and CLI behavior shared.

## Task 3: Cancellation and Host Lifecycle

**Files:** PluginProcessor.h/.cpp; ChordPreviewChecks.cpp.

**Interfaces:** use Task 2 methods and atomic epochs; no new APVTS IDs. `setPreviewInputPending(false)` only permits new input; it never restores an old request.

Add message-thread overload `juce::File createDragClipFile(juce::String& errorMessage, const juce::File& outputDirectory);`. The existing one-argument method delegates to it with the current Documents/ChordText/Clips path. This narrow directory seam lets checks exercise the same drag writer in an isolated folder; it does not change production naming, bytes, or destination.

- [x] Add `checkPreviewCancellation()`: while a hold is sounding and while a request is queued, invalidate through pending text, valid/invalid/empty progression updates, each of `key`, `mode`, `scaleMode`, `voicing`, `range`, `velocity`, `loop`, project restore, bypass, prepareToPlay, and releaseResources. Assert no canceled request restarts and matching UI feedback becomes idle. Dispatch message-thread updates only where the existing architecture requires them.
- [x] Add `checkPreviewTransportIndependence()`: hold across stop/start, seeks, tempo changes, loop wrap, and transitions to missing/invalid PPQ/tempo. Assert no note retrigger/off until manual release or explicit cancellation. On release, assert silence when normal playback is unavailable or beyond the end of a non-looping progression.
- [x] Add `checkPreviewStateAndFiles()`: compare serialized processor state and exported/drag MIDI bytes before and during preview. Verify no preview properties appear and two processor instances have independent desired/output state. Use isolated temporary files via the directory overload defined above, not real user settings, Documents drag storage, or installed bundles.
- [x] Run focused checks to identify actual gaps before fixes.
- [x] Delegate the existing drag creation body through the explicit-directory overload. Use that overload in checkPreviewStateAndFiles to compare before/during preview drag bytes in a unique build/Tests folder. No separate MIDI writer or export-plan copy is introduced.
- [x] In parameterChanged, preserve the existing internal mode-synchronization guard, then cancel for every external musical parameter change. Advance data revision for parameters that require rebuilt voicings/velocity. For Loop, cancel but do not invalidate reusable pitch data or introduce a new plan publication merely to refresh preview eligibility.
- [x] Cancel and invalidate eligibility in progression/state-rebuild paths. In state restore, invalidate immediately before deferred message-thread work. Do not change last-valid normal playback behavior on parse errors.
- [x] Bypassed processing emits pending releases and releases all tracked output, invalidates requests, clears feedback, and never emits a new note-on. Mark the bypass interval so requests created during it cannot replay on the first normal block. A fresh post-unbypass gesture is required.
- [x] prepareToPlay first caches all sounding pitches for next-block releases, then cancels preview and resets applied ownership. releaseResources cancels/clears tracking without emitting MIDI and preserves the documented host-flush limitation. Ordinary plan/preview transitions must not discard pending prepareToPlay releases before the next normal or bypassed callback emits them.
- [x] getPreviewFeedback consumes only the feedback mailbox on the message thread and rejects stale epoch/revision. Do not show a canceled HELD badge while waiting for another audio callback.
- [x] Run ProcessorTests. Require zero failures and confirm no accidental normal-note retrigger when no preview is active, especially on Loop changes and mode synchronization.

## Task 4: Card Gestures and Approved Visual States

**Files:** PluginEditor.h/.cpp; ChordPreviewChecks.cpp.

**Interfaces:** extend ChordSequenceComponent with message-thread callbacks:

- `std::function<chordscript::PreviewGestureId(int, std::uint64_t)> onMomentaryPreview;`
- `std::function<void(chordscript::PreviewGestureId)> onMomentaryRelease;`
- `std::function<void(int, std::uint64_t)> onToggleHeldPreview;`
- `void setPreviewFeedback(chordscript::PreviewFeedback feedback);`
- CardContent forwards these callbacks via its owning sequence component. Extend setRows with a fourth `std::uint64_t displayedRevision` argument. Store it with the displayed rows and carry it into every card-start/toggle callback. The editor reads getPreviewCardRevision alongside getLearningRows on the message thread. Include displayed revision and feedback in the repaint fast path.
- CardContent adds shared `juce::Rectangle<int> cardBounds(int index) const` and `int cardAt(juce::Point<int>) const` for paint/hit geometry, plus mouseDown/mouseUp handlers and a stored momentary gesture ID. A release sent by an older gesture must not clear a newer one.

- [x] Add `checkPreviewCardGestures()` with JUCE MouseEvent fixtures. Assert left-down maps each card to its index, left-up releases the returned ID even outside bounds, right-down toggles without a second toggle on mouse-up, gaps/background do nothing, and stale/empty rows reject gestures. Exercise a synthesized capture-loss mouseUp with a matching momentary ID, and assert a stale release cannot stop a held gesture. Native simultaneous-button combinations are out of scope: JUCE suppresses secondary button-down delivery while another button is down. Test replacing momentary with held through the processor APIs in Task 2, not as a claim about physical simultaneous-button delivery.
- [x] Add `checkPreviewScaledAndScrolledCards()` at 75%, 100%, 125%, and 150%. Scroll a long sequence and convert editor coordinates through the transformed hierarchy. Assert hit tests select the intended visible card and release ends its preview. Zoom during a hold must not cancel or retrigger it.
- [x] Add `checkPreviewDisplayedRevision()`: render valid cards, rebuild the processor with different chord/key/state data before the editor timer refresh, then click the old visible card. Assert beginMomentaryPreview returns 0 and right-toggle starts nothing for its old displayed revision. Refresh rows/revision and assert the same index now plays the newly displayed chord. Also invalidate immediately through an automation callback before its deferred rebuild.
- [x] Add `checkPreviewEditorCleanup()`: text onChange marks input pending and cancels before the 150ms debounce; normal apply clears pending; editor destruction cancels before flushing a pending edit. Ordinary focus loss leaves a right hold active; momentary capture loss releases it. Reopened editors start without a hold.
- [x] Build/run and record failing assertions or missing interfaces.
- [x] Share cardBounds between paint and cardAt, with row/column/gap checks and existing viewport coordinate conversion. Use JUCE's captured mouse release behavior; do not implement global polling. On Windows, JUCE 8.0.15 handles WM_CAPTURECHANGED by synthesizing mouseUp; route that through the matching momentary-ID release. Do not add a nonexistent Component::mouseCaptureLost override, a native hook, or another timer. Editor destruction explicitly cancels through the processor. Treat touch/keyboard audition and hover-glissando as out of scope.
- [x] Wire callbacks to Task 2 processor methods. Store returned momentary IDs, ignore rejected ID 0, and retain processor ownership of hold toggling. Do not infer right-toggle state from delayed audio feedback.
- [x] Update editor onTextChange, applyProgressionText, and destructor with pending/cancel calls. Keep current preview polling in the existing 30Hz editor timer; no additional watcher/thread.
- [x] Match the approved mockup: teal sounding card, PREVIEW for momentary, monochrome pin + HELD for latched, PLAYING only for normal sounding progression. Suppress normal active highlight during manual output; preserve host transport status. Add `Left press to preview · Right-click to hold` and concise preview status within existing compact areas. UI-scale save failures and diagnostics retain precedence/visibility.
- [x] Capture idle, momentary, held, normal playback, pending, and invalid renders using the optional snapshot harness. Check all four scales for badge/hint/status fit. Source screenshots live under the ignored task directory.
- [x] Run ProcessorTests and inspect renders. Report transformed event checks separately from actual native window capture/focus behavior, which still needs a host check.

## Task 5: Documentation, Review, and Project Verification

**Files:** README.md; approved spec/plan status; execution ledger.

- [x] Document left press/release, right toggle, single preview, same-card retrigger, no hold restoration, current-position resume, and settings/lifecycle cancellation. Explain that preview is per instance, transient, and routed to another instrument.
- [x] Document that stopped/no-playhead preview requires the host to keep processing. Add manual checklist cases for stopped and playing transport, release outside the plugin, right hold while using the DAW, changing settings, bypass/reset, zoom/scroll, and resume at current timeline position. Do not mark unperformed host checks passed.
- [x] Run `build.bat test` (or the current documented wrapper after PR #12). Require a built Release VST3, zero failures in TheoryTests/ProcessorTests/CliTests, and no new compiler warnings. Report actual counts; do not assume they match the previous feature.
- [x] Run git diff --check, check modified Markdown links, inspect status/diff for unrelated files, and confirm APVTS IDs/state migration/theory/CLI remain unchanged. Building does not authorize installation.
- [x] Request one fresh independent final code review against the feature base, specifically covering mailbox ownership, multi-thread invalidation, gesture cancellation, lifecycle release, and exact-boundary resume. Resolve important findings and rerun affected checks.
- [x] Mark spec and plan complete with actual verification evidence and any unperformed host checks. Report the result back to the coordinator. Do not send a message to another user-owned chat without direct user authorization; a child agent's final response is sufficient.
- [x] Stop with the reviewed local changes ready for the user. Commit/push/PR/merge require a subsequent user request under this handoff.

## Plan Self-Review

- Requests and feedback are independent SPSC channels; cancellation is a separate multi-caller atomic operation. No audio callback publishes into the message-owned request channel.
- Pitch payloads come from the existing message-thread voicer, not UI strings. Cache revision and cancellation epoch serve different purposes: reusable pitches survive editor close/Loop changes, while gestures do not.
- Tasks 1–3 own processor behavior and cancellation. Task 4 owns native input/visual feedback and debounce/editor cleanup. Task 5 owns documentation, review, and final verification.
- All five Review Focus conditions have named checks. Normal scheduler regressions remain in the existing suite. Handoff uses one implementation agent and a separate final reviewer, rather than a new implementer per small task.
- The approved timing choice permits coalescing completed gestures. Same-card retrigger still uses gesture identity even when pitches are unchanged.
- No product code, tests, build files, branches, or installed bundles were changed during planning. These documents are the only intended changes before approval.

## Independent Plan Review Record

The reviewer confirmed the two SPSC triple-buffer ownership schemes and the exact root-position MIDI assertion. Review identified displayed-card revision binding and JUCE capture delivery gaps; both are now specified. Additional checks cover a block crossing a chord boundary after preview release and drag-file output through an isolated-directory seam. Follow-up review found no remaining important planning gaps. Ready for user approval. No product code, tests, or builds were run during this review.


## Implementation Record

Completed 2026-10-24 on branch `codex/chord-preview`, based on commit 39496e6.
The changes are delivered on that branch for review.

Verification evidence:

- `build.bat test` built the Release VST3 and passed all three suites:
  TheoryTests 531 checks / 0 failures, ProcessorTests 16982 checks / 0 failures,
  CliTests 262 checks / 0 failures. The ProcessorTests count varies slightly because
  the mailbox stress check records how many coalesced publications the reader saw.
  No project warnings were observed.
- `git diff --check` is clean. `Source/Theory` and `Source/Cli` are unchanged. APVTS
  IDs, legacy mode migration, theory, and CLI behavior are unchanged.
- Renders were inspected for idle, momentary, held, normal playback, pending, and
  invalid states, and at 75%, 100%, 125%, and 150% scales.
- The independent final review found three Important issues: feedback stamped with
  live cancellation state, a drag that could write the old plan while an automation
  rebuild was pending, and a delayed UI release that could end a newer momentary
  gesture. All three were fixed with regression checks that failed before the fix;
  ProcessorTests is green afterwards.
- Unperformed host checks remain unperformed and are not marked passed: real DAW
  routing/audibility, native capture-loss and focus behavior, drag acceptance,
  stopped/playing transport, bypass/reset, and host zoom/scroll. They are listed in
  the README manual checklist.
