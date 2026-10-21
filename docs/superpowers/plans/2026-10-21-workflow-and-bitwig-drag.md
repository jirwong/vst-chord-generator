# Workflow Polish and Bitwig Drag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicit Regenerate button, show chord durations in the learning table, and let the user drag one loop into Bitwig as a MIDI clip while keeping file export.

**Architecture:** Extend the existing processor with learning-row durations and a two-mode MIDI writer (export vs drag). Add a small draggable preview component to the editor that writes a timestamped `.mid` file and starts an OS file drag. The file is retained so a Reveal action can recover from a host that ignores the drop.

**Tech Stack:** C++17, JUCE 8.0.15, CMake, MSVC x64, existing ChordText processor/editor/theory core.

**Spec:** `docs/superpowers/specs/2026-10-21-workflow-and-bitwig-drag-design.md`

## Global Constraints

- Windows x64 only. C++17. JUCE 8.0.15 and the standard library only.
- The plugin remains a VST3 MIDI effect with no audio buses.
- `processBlock` must not allocate, lock, perform file I/O, or destroy objects.
- All parsing, voicing, file writing, and UI work stays on the message thread.
- Editor size becomes fixed 760×640.
- Drag files are one loop, 960 PPQ, channel 1, note-offs before note-ons at equal ticks.
- Drag files omit tempo and time-signature meta events; export files keep them.
- Drag file folder: `Documents\ChordText\Clips`.
- Drag filenames: `ChordText-YYYYMMDD-HHMMSS.mid`, never overwriting an existing file.
- Export `.mid` behavior is unchanged.
- No automatic deletion of drag files.
- No native Bitwig API, individual-chord dragging, or multiple-loop dragging.
- Manual Bitwig verification remains a human gate.

## Review Focus

1. **Invalid text + Regenerate/drag** — expected: the last valid plan and rows remain, diagnostics appear, and dragging does not start. Pinned in Tasks 1 and 3.
2. **Drag timing and tempo** — expected: one loop, correct ticks, no tempo/time-signature events, note-offs before note-ons. Pinned in Task 2.
3. **File/folder failure** — expected: no drag starts, a clear error is shown, and Reveal is unavailable until a file exists. Pinned in Tasks 2 and 3.
4. **Editor regression** — expected: auto-parse still works, the Beats column fits, Regenerate is explicit, and Export still works. Pinned in Tasks 1 and 3.
5. **Bitwig drop acceptance** — expected: direct drop works, or the saved file can be revealed and dragged from Explorer. Pinned in Task 4's manual checklist.

---

### Task 1: Regenerate button and Beats column

**Files:**
- Modify: `Source/Plugin/PluginProcessor.h`
- Modify: `Source/Plugin/PluginProcessor.cpp`
- Modify: `Source/Plugin/PluginEditor.h`
- Modify: `Source/Plugin/PluginEditor.cpp`
- Modify: `Tests/ProcessorTest.cpp`

**Interfaces:**
- Consumes: existing `LearningRow`, `ProgressionStep`, `setProgressionText`, `rebuildPlan`, editor timer and table model.
- Produces: `LearningRow::durationBeats` (`double`, default `4.0`); editor component IDs `progressionEditor`, `regenerateButton`, `learningTable`; `void applyProgressionText (bool force)`; fixed editor height 640.

- [ ] **Step 1: Write the failing processor duration test**

Append to `Tests/ProcessorTest.cpp`:

```cpp
// learning rows carry the effective duration in beats
{
    ChordTextAudioProcessor p;
    p.setProgressionText ("Cmaj7:2, G7");
    const auto rows = p.getLearningRows();
    CHECK (rows.size() == 2);
    CHECK_NEAR (rows[0].durationBeats, 2.0, 1e-9);
    CHECK_NEAR (rows[1].durationBeats, 4.0, 1e-9);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: compile failure because `LearningRow::durationBeats` does not exist.

- [ ] **Step 3: Add `durationBeats` and populate it**

In `PluginProcessor.h`, extend `LearningRow`:

```cpp
struct LearningRow
{
    juce::String chord;
    juce::String degree;
    juce::String notes;
    double durationBeats = 4.0;
};
```

In `rebuildPlan()`, copy `step.durationBeats` into each row before pushing it.

- [ ] **Step 4: Write the failing editor tests**

Append to `Tests/ProcessorTest.cpp`:

```cpp
// editor exposes the new controls and can force a rebuild
{
    ChordTextAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    CHECK (editor != nullptr);
    CHECK (editor->getWidth() == 760);
    CHECK (editor->getHeight() == 640);

    auto* progression = dynamic_cast<juce::TextEditor*> (
        editor->findChildWithID ("progressionEditor"));
    auto* regenerate = dynamic_cast<juce::TextButton*> (
        editor->findChildWithID ("regenerateButton"));
    CHECK (progression != nullptr);
    CHECK (regenerate != nullptr);

    progression->setText ("C", juce::dontSendNotification);
    regenerate->triggerClick();
    CHECK (p.getLearningRows().size() == 1);
    CHECK (p.getLearningRows()[0].chord == "C");

    progression->setText ("H", juce::dontSendNotification);
    regenerate->triggerClick();
    CHECK (p.getLearningRows().size() == 1);
    CHECK (! p.getDiagnostics().empty());
}
```

- [ ] **Step 5: Run the tests to verify they fail**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: failures for editor height/component lookup and missing button.

- [ ] **Step 6: Implement Regenerate and the Beats column**

In `PluginEditor.h`:

- add `juce::TextButton regenerateButton { "Regenerate" };`
- change `void applyProgressionText();` to `void applyProgressionText (bool force);`

In `PluginEditor.cpp`:

- set `progressionEditor.setComponentID ("progressionEditor")`,
  `regenerateButton.setComponentID ("regenerateButton")`,
  `table.setComponentID ("learningTable")`.
- set the editor size to `760, 640`.
- `regenerateButton.onClick = [this] { applyProgressionText (true); refreshStatus(); };`
- `timerCallback` calls `applyProgressionText (false)`.
- update the destructor's pending-text flush call to `applyProgressionText (false)`.
- update the existing editor smoke test's height expectation from 560 to 640.
- `applyProgressionText(bool force)`:

```cpp
void ChordTextAudioProcessorEditor::applyProgressionText (bool force)
{
    textEditPending = false;
    const auto text = progressionEditor.getText();

    if (force || text != processor.getProgressionText())
        processor.setProgressionText (text);
}
```

- call `addAndMakeVisible (regenerateButton)`.
- add a fourth table column `"Beats"` with ID 4 and width 70.
- in `LearningTableModel::paintCell`, format `rowFor(rowNumber)->durationBeats`:
  - integers as `4`, `2`;
  - fractional values with up to 3 decimals, trimming trailing zeros and a trailing dot, so `1.5` stays `1.5`.
- adjust column widths so the four columns fit the 760-wide editor: Chord 150, Degree 150, Notes 300, Beats 70.
- place `Regenerate` on the progression header row, right-aligned.

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: `0 failures`.

- [ ] **Step 8: Commit**

```bash
git add Source/Plugin/PluginProcessor.h Source/Plugin/PluginProcessor.cpp \
        Source/Plugin/PluginEditor.h Source/Plugin/PluginEditor.cpp \
        Tests/ProcessorTest.cpp
git commit -m "Add regenerate button and chord duration column"
```

---

### Task 2: Two-mode MIDI writer and drag clip files

**Files:**
- Modify: `Source/Plugin/PluginProcessor.h`
- Modify: `Source/Plugin/PluginProcessor.cpp`
- Modify: `Tests/ProcessorTest.cpp`

**Interfaces:**
- Consumes: existing `exportPlan`, `lastHostBpm`, `diagnostics`, `exportMidiFile`.
- Produces:
  - private `enum class MidiFileMode { exportFile, dragClip };`
  - private `bool writeMidiFile (const juce::File&, MidiFileMode, juce::String&);`
  - public `juce::File createDragClipFile (juce::String& errorMessage);`
  - public `juce::File getLastDragClipFile() const;`
  - private member `juce::File lastDragClipFile;`

- [ ] **Step 1: Write the failing drag-file test**

Append to `Tests/ProcessorTest.cpp`:

```cpp
// drag clip is one loop, with no tempo or time-signature metadata
{
    ChordTextAudioProcessor p;
    p.setProgressionText ("Cmaj7:2, G7:2");

    juce::String error;
    const auto file = p.createDragClipFile (error);
    CHECK (error.isEmpty());
    CHECK (file.existsAsFile());
    CHECK (p.getLastDragClipFile() == file);
    CHECK (file.getParentDirectory().getFileName() == "Clips");
    CHECK (file.getParentDirectory().getParentDirectory().getFileName() == "ChordText");
    CHECK (file.getFileName().startsWith ("ChordText-"));

    juce::FileInputStream in (file);
    CHECK (in.openedOk());
    juce::MidiFile midiFile;
    CHECK (midiFile.readFrom (in));

    int noteOns = 0, noteOffs = 0, tempoEvents = 0, timeSignatureEvents = 0;
    std::vector<double> noteOnTicks;
    std::vector<double> noteOffTicks;

    for (int t = 0; t < midiFile.getNumTracks(); ++t)
    {
        const auto* track = midiFile.getTrack (t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto* event = track->getEventPointer (i);
            const auto message = event->message;

            if (message.isNoteOn())
            {
                ++noteOns;
                noteOnTicks.push_back (event->message.getTimeStamp());
                CHECK (message.getChannel() == 1);
            }
            if (message.isNoteOff())
            {
                ++noteOffs;
                noteOffTicks.push_back (event->message.getTimeStamp());
            }
            if (message.isTempoMetaEvent()) ++tempoEvents;
            if (message.isTimeSignatureMetaEvent()) ++timeSignatureEvents;
        }
    }

    CHECK (noteOns == 8);
    CHECK (noteOffs == 8);
    CHECK (tempoEvents == 0);
    CHECK (timeSignatureEvents == 0);
    CHECK (noteOnTicks.size() == 8);
    CHECK (noteOffTicks.size() == 8);

    for (int i = 0; i < 4; ++i)
        CHECK (noteOnTicks[(size_t) i] == 0.0);
    for (int i = 4; i < 8; ++i)
        CHECK (noteOnTicks[(size_t) i] == 2.0 * 960.0);
    for (int i = 0; i < 4; ++i)
        CHECK (noteOffTicks[(size_t) i] == 2.0 * 960.0);
    for (int i = 4; i < 8; ++i)
        CHECK (noteOffTicks[(size_t) i] == 4.0 * 960.0);

    file.deleteFile();
}

// invalid text produces no drag file
{
    ChordTextAudioProcessor p;
    p.setProgressionText ("H");
    juce::String error;
    const auto file = p.createDragClipFile (error);
    CHECK (! file.existsAsFile());
    CHECK (error.isNotEmpty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: compile failure because `createDragClipFile` does not exist.

- [ ] **Step 3: Refactor the MIDI writer**

Add to `PluginProcessor.h`:

```cpp
juce::File createDragClipFile (juce::String& errorMessage);
juce::File getLastDragClipFile() const;

private:
enum class MidiFileMode { exportFile, dragClip };
bool writeMidiFile (const juce::File& file, MidiFileMode mode,
                    juce::String& errorMessage);
juce::File lastDragClipFile;
```

Move the existing body of `exportMidiFile` into `writeMidiFile`:

- keep the diagnostics and empty-plan checks;
- always add the `Chord Text` track-name event and the note events;
- only add the tempo event and 4/4 time-signature event when `mode == exportFile`;
- use the same event sorting and note construction for both modes.

`exportMidiFile` becomes:

```cpp
bool ChordTextAudioProcessor::exportMidiFile (const juce::File& file,
                                              juce::String& errorMessage)
{
    return writeMidiFile (file, MidiFileMode::exportFile, errorMessage);
}
```

`createDragClipFile`:

```cpp
juce::File ChordTextAudioProcessor::createDragClipFile (juce::String& errorMessage)
{
    errorMessage.clear();

    if (! diagnostics.empty())
    {
        errorMessage = "Cannot create a clip while the progression has errors.";
        return {};
    }

    if (exportPlan.events.empty())
    {
        errorMessage = "Cannot create a clip for an empty progression.";
        return {};
    }

    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                            .getChildFile ("ChordText")
                            .getChildFile ("Clips");

    if (! folder.createDirectory())
    {
        errorMessage = "Cannot create the clips folder " + folder.getFullPathName() + ".";
        return {};
    }

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto file = folder.getNonexistentChildFile ("ChordText-" + stamp, ".mid", false);

    if (! writeMidiFile (file, MidiFileMode::dragClip, errorMessage))
        return {};

    lastDragClipFile = file;
    return file;
}
```

`getLastDragClipFile` returns `lastDragClipFile`.

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: `0 failures`.

- [ ] **Step 5: Confirm export still has tempo and time signature**

Extend the existing export read-back test in `Tests/ProcessorTest.cpp`:

```cpp
int timeSignatureEvents = 0;
// inside the event loop:
if (message.isTimeSignatureMetaEvent()) ++timeSignatureEvents;
// after the loop:
CHECK (timeSignatureEvents >= 1);
```

The existing tempo assertion must remain.

- [ ] **Step 6: Commit**

```bash
git add Source/Plugin/PluginProcessor.h Source/Plugin/PluginProcessor.cpp \
        Tests/ProcessorTest.cpp
git commit -m "Add drag clip writer with project-tempo MIDI"
```

---

### Task 3: Draggable clip preview and Reveal action

**Files:**
- Create: `Source/Plugin/ClipDragComponent.h`
- Create: `Source/Plugin/ClipDragComponent.cpp`
- Modify: `Source/Plugin/PluginEditor.h`
- Modify: `Source/Plugin/PluginEditor.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Tests/ProcessorTest.cpp`

**Interfaces:**
- Consumes: `ChordTextAudioProcessor::createDragClipFile`, `getLastDragClipFile`, editor `applyProgressionText`, `refreshStatus`, `getLearningRows`.
- Produces: `ClipDragComponent`; editor component IDs `clipDrag` and `revealClip`; `Reveal last clip` action.

```cpp
class ClipDragComponent final : public juce::Component
{
public:
    std::function<juce::File()> createClipFile;
    std::function<void (const juce::String&)> onStatus;

    void setLoopInfo (double beats, bool enabled);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    double loopBeats = 0.0;
    bool canDrag = false;
    bool dragStarted = false;
    juce::Point<int> dragStart;
};
```

- [ ] **Step 1: Write the failing component/editor tests**

Add `#include "Plugin/ClipDragComponent.h"` to `Tests/ProcessorTest.cpp`, then append:

```cpp
// clip preview enables only for a valid non-empty plan
{
    ChordTextAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    CHECK (editor != nullptr);

    auto* clip = dynamic_cast<ClipDragComponent*> (
        editor->findChildWithID ("clipDrag"));
    auto* reveal = dynamic_cast<juce::TextButton*> (
        editor->findChildWithID ("revealClip"));
    auto* progression = dynamic_cast<juce::TextEditor*> (
        editor->findChildWithID ("progressionEditor"));
    CHECK (clip != nullptr);
    CHECK (reveal != nullptr);
    CHECK (progression != nullptr);

    clip->setLoopInfo (16.0, true);
    CHECK (clip->isEnabled());
    clip->setLoopInfo (0.0, false);
    CHECK (! clip->isEnabled());
    clip->setLoopInfo (16.0, true);
    CHECK (! reveal->isEnabled());

    const auto file = clip->createClipFile();
    CHECK (file.existsAsFile());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    CHECK (reveal->isEnabled());
    file.deleteFile();

    progression->setText ("H", juce::dontSendNotification);
    const auto invalidFile = clip->createClipFile();
    CHECK (invalidFile == juce::File());
    CHECK (! p.getDiagnostics().empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: compile failure because `ClipDragComponent` and the component IDs do not exist.

- [ ] **Step 3: Implement `ClipDragComponent`**

Create `Source/Plugin/ClipDragComponent.h/.cpp` with the interface above.

`setLoopInfo`:

- store `loopBeats`;
- store `canDrag`;
- call `setEnabled (enabled)`;
- `repaint()`.

`paint`:

- draw a rounded clip rectangle using the editor palette;
- draw `Drag to DAW`;
- draw the loop description, for example `16 beats · 4 bars (4/4)`;
- when disabled, draw `Fix errors to drag` and reduce opacity.

`mouseDown`:

- store `dragStart`;
- reset `dragStarted = false`.

`mouseDrag`:

- return if `! canDrag` or `dragStarted`;
- start only after the mouse has moved more than 4 pixels;
- set `dragStarted = true`;
- call `createClipFile()`; if it returns an empty `File`, return;
- call:

```cpp
juce::StringArray files;
files.add (file.getFullPathName());
juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
```

- report `Clip saved: <filename>` through `onStatus`; if the drag could not start,
  report `Clip saved; drag could not start.`

- [ ] **Step 4: Wire the component into the editor**

In `PluginEditor.h` add:

```cpp
juce::TextButton revealClipButton { "Reveal last clip" };
ClipDragComponent clipDrag;
```

In the constructor:

- `clipDrag.setComponentID ("clipDrag");`
- `revealClipButton.setComponentID ("revealClip");`
- `addAndMakeVisible (clipDrag);`
- `addAndMakeVisible (revealClipButton);`
- `revealClipButton.setEnabled (false);`
- `revealClipButton.onClick = [this]
  {
      const auto file = processor.getLastDragClipFile();
      if (file.existsAsFile())
          file.revealToUser();
  };`
- `clipDrag.createClipFile = [this]
  {
      applyProgressionText (true);

      if (! processor.getDiagnostics().empty())
      {
          lastExportStatus = "Fix the progression errors before dragging.";
          refreshStatus();
          return juce::File();
      }

      juce::String error;
      const auto file = processor.createDragClipFile (error);

      if (file == juce::File())
      {
          lastExportStatus = "Drag failed: " + error;
          refreshStatus();
      }

      return file;
  };`
- `clipDrag.onStatus = [this] (const juce::String& status)
  {
      lastExportStatus = status;
      refreshStatus();
  };`

In `refreshStatus`:

- compute `const auto rows = processor.getLearningRows();`
- `double totalBeats = 0.0;` sum `row.durationBeats`;
- `const bool canDrag = diagnostics.empty() && ! rows.empty();`
- `clipDrag.setLoopInfo (totalBeats, canDrag);`
- `revealClipButton.setEnabled (processor.getLastDragClipFile().existsAsFile());`

In `resized`, allocate the clip preview and Reveal button:

- place Reveal next to Export;
- place the clip preview below the table or beside the export row, sized to fit
  the 760×640 editor.

Add `Source/Plugin/ClipDragComponent.cpp` to `PLUGIN_SOURCES` in `CMakeLists.txt`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build --config Release --target ProcessorTests
./build/Tests/ProcessorTests.exe
```

Expected: `0 failures`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt Source/Plugin/ClipDragComponent.h \
        Source/Plugin/ClipDragComponent.cpp Source/Plugin/PluginEditor.h \
        Source/Plugin/PluginEditor.cpp Tests/ProcessorTest.cpp
git commit -m "Add draggable MIDI clip preview and reveal action"
```

---

### Task 4: Documentation and manual Bitwig verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: all prior tasks.
- Produces: documented Regenerate, Beats column, drag workflow, fallback, and limitations.

- [ ] **Step 1: Update the README**

Add sections covering:

- the Regenerate button and that auto-parse remains;
- the Beats column and default duration;
- the draggable clip preview and the `Documents\ChordText\Clips` folder;
- project-tempo behavior for dragged clips;
- the Reveal last clip fallback;
- the fact that export still includes tempo/4-4 metadata;
- the manual Bitwig checklist;
- known limitations: Bitwig acceptance, file accumulation, no native clip creation.

- [ ] **Step 2: Run the full test suite**

```bash
./build.sh --test
```

Expected: `TheoryTests` and `ProcessorTests` both report `0 failures`, and the
VST3 is present under `build/ChordText_artefacts/Release/VST3/`.

- [ ] **Step 3: Manual Bitwig verification**

In Bitwig:

1. Load Chord Text with a valid progression.
2. Confirm the Beats column shows default and explicit durations.
3. Change the text and press Regenerate; confirm the table updates immediately.
4. Enter invalid text and press Regenerate; confirm the previous rows/plan stay.
5. Drag the clip preview into the arranger or onto a track.
6. Confirm a MIDI clip appears with the expected notes and loop length.
7. Confirm it follows the project tempo.
8. If the drop is ignored, click Reveal last clip and drag the file from Explorer.
9. Confirm Export `.mid` still works and retains tempo/4-4 metadata.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "Document regenerate, beats, and Bitwig drag workflow"
```

---

## Verification Summary

| Spec requirement | Verified by |
|---|---|
| Regenerate forces rebuild | Task 1 editor test |
| Auto-parse remains | Task 1 timer code review, Task 4 manual check |
| Beats column | Task 1 processor test, Task 4 manual check |
| Invalid text preserves plan | Task 1 editor test |
| Drag file one loop / ticks | Task 2 processor test |
| Drag file has no tempo/signature | Task 2 processor test |
| Export still has tempo/signature | Task 2 export test |
| Clip preview enabled only when valid | Task 3 editor test |
| Reveal enabled after a drag file | Task 3 editor test |
| Bitwig direct drop and fallback | Task 4 manual checklist |
| Full build and tests | Task 4 `./build.sh --test` |
