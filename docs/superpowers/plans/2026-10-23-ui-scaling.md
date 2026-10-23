# UI Scaling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the approved footer scale selector and save one local preference for future editor windows.

**Architecture:** Keep the design at 960 by 650 logical pixels inside a transformed content component. Resize the outer editor to the selected percentage and leave JUCE's host scale transform intact. Read and write the preference through a small UI-only helper with an injectable file path.

**Tech Stack:** C++17, JUCE 8.0.15, Windows x64, Visual Studio 2022/CMake, existing CHECK-based tests.

**Spec:** [Approved UI scaling design](../specs/2026-10-23-ui-scaling-design.md).

## Global Constraints

- Offer 75%, 100%, 125%, and 150%. Default to 100%.
- Use window sizes 720 x 488, 960 x 650, 1200 x 813, and 1440 x 975 before host display scaling.
- Keep the percentage dropdown at the right side of the footer. Its label is UI SCALE.
- Change the current editor immediately. Other open editors keep their size and label until reopened.
- Save outside DAW state at `%LOCALAPPDATA%\Chordscript\ui-settings.json` for the current Windows user.
- Keep all APVTS IDs, musical behavior, CLI behavior, and plugin identity unchanged.
- Preserve text, selection, pending edits, validation, and playback when scaling.
- Run file operations and editor changes on the message thread.
- Do not add free resizing, extra percentages, screen fitting, or a live preference watcher.
- Preserve unrelated files and branches. Use trash-cli if a file must be removed.

## Review Focus

- Host scaling and user zoom must combine; user changes must not reset the host transform (Task 2).
- Two editors may display different scales; only a newly opened editor reads the latest saved preference (Task 3).
- A failed save must keep the current zoom, report failure, and leave the last complete saved file intact (Tasks 1 and 3).
- Pending text and active MIDI must survive a zoom without an extra parse, plan replacement, or note retrigger (Tasks 2 and 3).
- Fractional zoom must keep footer controls, menus, hit testing, and scrolling aligned with the logical layout (Tasks 2 and 4).

## Files and Responsibilities

- Create `Source/Plugin/UiSettings.h` and `UiSettings.cpp` for validated local storage.
- Modify `CMakeLists.txt` to add UiSettings.cpp to PLUGIN_SOURCES.
- Modify `Source/Plugin/PluginEditor.h` and `PluginEditor.cpp` for logical content, zoom, and footer controls.
- Modify `Tests/ProcessorTest.cpp` for isolated preference, editor, state, and playback tests.
- Modify `README.md` to explain scale controls and local persistence.
- No theory, CLI, processor state, or scheduling changes are needed.

## Execution Setup

- [x] Use the existing native execution method and its task ledger.
- [x] Inspect current Git status and the repository guidance. The checkout was on `docs/agents-md` during planning. Do not put feature code on that unrelated branch.
- [x] Use superpowers:using-git-worktrees to establish a suitable feature checkout. Preserve these two design/plan documents. Reuse the writable checkout and installed JUCE dependency if the managed worktree cannot build.
- [x] Use feature branch `codex/ui-scaling` from current `main`. Preserve any work the user does in parallel.
- [x] Locate CMake with Get-Command or the Visual Studio Build Tools path in build.bat. Below, `$cmakeExe` means that executable. Use VS 2022 x64, not Ninja.
- [x] Inspect prior build results. Repeat a baseline only if code changes or uncertainty invalidate the last successful build.

## Task 1: Local UI Preferences

**Files:** new UiSettings.h/.cpp; CMakeLists.txt; Tests/ProcessorTest.cpp.

**Interfaces:**
- `class UiSettings` is in the plugin layer and owns a `juce::File` path, not a process-wide settings cache.
- `UiSettings()` selects the normal local file. `explicit UiSettings(juce::File file)` selects a supplied file for tests.
- `static juce::File defaultFile();`
- `static bool isSupportedScale(int percent);`
- `int loadUiScalePercent() const;`
- `juce::Result saveUiScalePercent(int percent) const;`
- An explicit empty File is invalid storage: load returns 100 and save returns failure. It must not fall back to real user storage.

- [x] Add tests in ProcessorTest.cpp using unique paths under ignored `build/Tests/ui-settings/`. Check that a missing file returns 100 without creating a file. Check all four supported values and rejection of unsupported values.
- [x] Check malformed JSON, missing key, arrays, null, string values, fractional numbers, and unsupported integers all load as 100. Store only integer `uiScalePercent` values.
- [x] Check two separate UiSettings objects see each other's completed saves on the next load. Check a failed write using a regular file where the parent directory should be. Check failed validation does not replace an existing valid preference.
- [x] Build ProcessorTests with `& $cmakeExe --build build --config Release --target ProcessorTests --parallel`. Confirm failure because the helper is not implemented.
- [x] Implement defaultFile using LOCALAPPDATA and the agreed path. If LOCALAPPDATA is unavailable, return an invalid File. Do not fall back to a roaming or machine-wide administrator location.
- [x] Parse a JSON object on each load. Accept only integral JSON values from the four supported choices. Do not write during reads. Return a failed Result for invalid storage or unsupported save values.
- [x] Create the parent directory as needed. Write a complete JSON object through `juce::TemporaryFile`, flush and close its output stream, and replace the target only after the write succeeds. Concurrent processes use the last successful atomic replacement; do not add live synchronization or locks for unrelated state.
- [x] Add UiSettings.cpp to PLUGIN_SOURCES. Run the focused build and `build/Tests/ProcessorTests.exe`; require zero failures. Tests must not write to real LOCALAPPDATA.

## Task 2: Logical Layout and Footer Zoom

**Files:** PluginEditor.h/.cpp; Tests/ProcessorTest.cpp.

**Interfaces:**
- Keep the production editor constructor callable with only a processor. Extend it with an optional UiSettings value: `ChordTextAudioProcessorEditor(ChordTextAudioProcessor&, UiSettings settings = UiSettings{});`.
- Add a private `ContentComponent : public juce::Component` with `paint(juce::Graphics&) override` for the existing background, header, footer, and separator drawing.
- Add private `layoutContent()` and `applyUiScale(int percent)` methods.
- Store the supplied UiSettings, an integer appliedUiScalePercent, the content component, a Label, and a ComboBox.
- Component IDs: `editorContent`, `uiScaleLabel`, and `uiScaleBox`. Keep all existing control IDs.

- [x] Add a `createTestEditor(ChordTextAudioProcessor&)` helper in ProcessorTest.cpp. It constructs the concrete editor with an explicit empty settings File, so existing editor tests stay at 100% and do not read user preferences.
- [x] Update existing editor construction sites to use that helper. Add a recursive `findEditorChild(juce::Component&, const juce::String&)` test helper and update direct-child searches. The new content component changes hierarchy, not the controls' behavior.
- [x] Add tests for a missing injected preference and for all four choices. Check exact dimensions, the dropdown value, label, and each important child's transformed bounds. Use `editor.getLocalArea(child, child->getLocalBounds())` for visible bounds; raw child bounds stay logical.
- [x] Check footer status and selector do not overlap, Loop stays inside its margin, and drag/export/reveal heights still match. Check the logical layout stays at 960 x 650 at every percentage, rather than changing the number of card columns.
- [x] Add host-composition tests: call the base setScaleFactor at 1.25 and 1.5, select user zooms, and assert the editor's host transform is unchanged. Confirm content transforms contain only user zoom and scaled screen coordinates reflect both factors.
- [x] Build/run ProcessorTests to observe the missing scaling behavior before implementing it.
- [x] Add content as the editor's child and parent the current controls to it, including controls added by setup lambdas. Move background/header/footer drawing into ContentComponent. Root paint only fills any rounding gap. Keep the content at bounds (0,0,960,650).
- [x] Move current resized layout into layoutContent. Base geometry on content.getLocalBounds()/getWidth(), not the scaled outer editor. Route validation layout refresh through layoutContent.
- [x] Add UI SCALE and a four-choice ComboBox to the footer. Reserve 86 logical pixels for the box, 10 for the gap, and 65 for its label. Keep the existing left footer margin and fit status text in the remaining width. Use the current charcoal/teal control styling and a 28-pixel box height.
- [x] Load the preference once during construction. Set the ComboBox without notification. Apply a content AffineTransform for percent/100 and resize the editor with nearest-integer dimensions. Do not override or call setScaleFactor for user zoom. Do not allow free window resizing.
- [x] On a changed dropdown selection, applyUiScale updates only UI geometry, saves the preference, and refreshes status. It must not call applyProgressionText, setProgressionText, rebuildPlan, or recreate controls. A save error adds a concise footer message such as `UI scale was not saved` without hiding progression errors.
- [x] Run ProcessorTests and require zero failures. Verify a representative transformed point maps back to the expected control. Native popup positioning remains a manual host check.

## Task 3: Persistence, Project State, and Playback

**Files:** Tests/ProcessorTest.cpp; PluginEditor.cpp only if integration exposes a defect.

**Interfaces:** Use UiSettings and the constructor from Tasks 1 and 2. No processor API or parameter changes.

- [x] Open two editors using separate UiSettings objects with the same file. Both start at 100%. Set the first to 125%. Assert its size is 1200 x 813; the second stays at 960 x 650 and still shows 100%.
- [x] Close/reopen the second editor and assert it uses 125%. Repeat after a save through a third settings object, representing another process. Do not add an in-process cache that hides the new disk value.
- [x] Add a failed-save editor test. Selecting 150% still changes that editor to 1440 x 975 and shows the save error. A newly opened editor uses the last valid saved value or 100% if none exists. Confirm the preference bytes remain unchanged on failed validation.
- [x] Save and restore processor state while an editor is open at a nondefault scale. Assert the window size and dropdown do not change. Check serialized project state has no uiScalePercent property and the parameter list/IDs are unchanged.
- [x] Start a pending text edit, select a new scale before the 150ms debounce expires, and compare text, caret/selection, and action state immediately. Then dispatch messages past the debounce and check the new progression is applied once as usual.
- [x] With FakePlayHead, start a sustained chord and process consecutive blocks around a scale selection. Assert no new note-on, early note-off, or plan-change retrigger occurs on the second block. Stop transport and verify the original notes are released.
- [x] Use meaningful assertions for added behavior. Observe any new regression fail before fixing product code. If the code already meets an integration assertion, record it as additional verification rather than claiming a new red/green cycle.
- [x] Build/run ProcessorTests; require zero failures. Keep file fixtures and generated images under ignored build or task-workspace directories.

## Task 4: Renders, Documentation, and Full Verification

**Files:** Tests/ProcessorTest.cpp snapshot harness; README.md.

- [x] Extend the optional CHORDSCRIPT_UI_SNAPSHOT_DIR harness to render valid and invalid input at all four percentages. Truncate an existing PNG output stream before writing a new image.
- [x] Inspect all eight renders. Check scaled text, card proportions, diagnostics, footer alignment, Loop, and MIDI action heights. Check 75% and 150% explicitly. Verify long status text clips within its reserved area.
- [x] Document the four choices, 100% default, local Windows-user preference path, and current-window/reopen behavior. Explain that scale is outside project state and is not an automation parameter.
- [x] Run `build.bat test`. Require a built VST3 and zero failures in TheoryTests, ProcessorTests, and CliTests. Check project code for warnings. Do not repeat the full run after success without a new change or unresolved concern.
- [x] Run `git diff --check`. Check local documentation links. Inspect the full diff and Git status for unrelated files.
- [x] Request one independent final review using the native execution workflow. Resolve important findings and rerun the affected checks.
- [x] Report automated resize/input checks and JUCE renders separately from actual DAW resize, menu, display-DPI, and external-drag checks. Keep unperformed host checks explicit.
- [x] Mark the design and plan complete with actual results. Do not commit, push, create a PR, or merge unless the user requests that action.

## Self-Review Record

- Storage rules and failure behavior map to Task 1. Editor layout and host scale composition map to Task 2.
- Shared preference semantics, project isolation, and playback preservation map to Task 3. Renders, documentation, build, and review map to Task 4.
- All five Review Focus conditions have assigned checks. Existing control IDs survive the new content hierarchy.
- UiSettings file injection isolates tests from real user preferences. No dependency, APVTS parameter, theory API, or CLI change is planned.
- At approval time, this plan had not changed product code or Git branch state.

## Implementation Record — 2026-10-24

Implemented on `codex/ui-scaling`. Full Release VST3 build passed without compiler warnings. TheoryTests: 531 checks, 0 failures. ProcessorTests with render capture enabled: 620 checks, 0 failures. CliTests: 262 checks, 0 failures. All eight JUCE valid/invalid renders were inspected. Documentation links and `git diff --check` passed.

Independent review found two issues, both resolved: preference replacement now uses one Windows `MoveFileExW` operation after flushing and closing a same-directory temporary file; preference save errors appear before potentially long export messages. Follow-up review found no remaining important issues. Concurrent writes may return failure under Windows contention; tests require a complete final value from a successful writer. Failed saves retain current editor zoom and show an error.

Actual DAW window resizing, native popup menus, mixed-DPI displays, and external drag acceptance remain unperformed manual host checks. Automated geometry, hit testing, scrolling, host-scale composition, editing, project-state isolation, and playback checks passed. No commit, push, PR, or merge was performed.