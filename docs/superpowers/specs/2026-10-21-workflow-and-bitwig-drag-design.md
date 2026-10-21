# Chord Text — Workflow Polish and Bitwig Drag Design

Date: 2026-10-21
Status: Approved design, pending written-spec review

## 1. Intent

Improve the Chord Text workflow in three ways:

1. Add an explicit **Regenerate** control so the current text can be parsed and
   rebuilt immediately, without waiting for the automatic debounce.
2. Show each chord's effective duration in the learning table.
3. Let the user drag one complete loop from the editor into Bitwig as a MIDI
   clip, while keeping the existing `.mid` export.

Live MIDI playback already works with Bitwig's transport and is unchanged.

### Success criteria

1. The Regenerate button immediately parses the current text and rebuilds the
   playback plan and learning table.
2. Automatic parsing after 150 ms still works; Regenerate supplements it.
3. Invalid text does not replace the last valid plan or rows; diagnostics are
   shown.
4. The learning table has a Beats column showing the effective duration of each
   chord.
5. The editor has a draggable clip preview that is enabled only for a valid,
   non-empty progression.
6. Dragging creates one loop of MIDI at the current key, voicing, range, and
   velocity settings.
7. The dragged clip is written as a real `.mid` file under
   `Documents\ChordText\Clips`.
8. A "Reveal last clip" action opens Explorer with the most recently created
   drag file selected.
9. The existing Export `.mid` behavior is unchanged.
10. The normal theory and processor test suites pass.

### Out of scope

- Native Bitwig clip creation or a Bitwig-specific extension API.
- Dragging individual chords or individual table rows.
- Dragging multiple loop repetitions.
- Automatic cleanup of old drag files.
- Drag-and-drop support verified in DAWs other than Bitwig.
- Changing live playback, transport sync, or note scheduling.

## 2. Current state

The editor currently has:

- a multi-line progression text editor with a 150 ms debounce,
- key, mode, voicing, range, velocity, and loop controls,
- a Chord / Degree / Notes learning table,
- a diagnostics area,
- an Export `.mid` button,
- an async `FileChooser`.

The processor already owns an immutable `RenderPlan`, a `learningRows` vector,
and `exportMidiFile()`. `LearningRow` currently contains chord, degree, and
notes strings.

## 3. Regenerate button

Add a `Regenerate` button beside the progression editor.

Behavior:

- Clicking it cancels any pending debounce and immediately calls the same path
  as `applyProgressionText()`.
- If the text has changed, `setProgressionText()` reparses and rebuilds the plan.
  If it has not changed, `rebuildPlan()` is called so the button still forces a
  rebuild.
- On success, the table, diagnostics, current step, and export state update.
- On failure, the last valid plan and rows remain; diagnostics are shown.
- Automatic parsing remains enabled.

No keyboard shortcut is added in this version.

## 4. Beats column

Add `double durationBeats` to `LearningRow`, populated from
`ProgressionStep::durationBeats`.

The learning table becomes:

| Chord | Degree | Notes | Beats |
|---|---|---|---|
| Cmaj7 | Imaj7 | C E G B | 4 |
| Am7 | vi7 | A C E G | 2 |
| Dm7 | ii7 | D F A C | 4 |
| G7 | V7 | G B D F | 1.5 |

- The value is the effective duration, including the default 4 beats when the
  input has no `:N` suffix.
- It is displayed as a compact number, not `4 beats`.
- The table continues to show the last valid progression while the text is
  invalid.
- Column widths are adjusted so the four columns fit the existing editor width.

## 5. Drag-to-Bitwig

### 5.1 Approach

Use an external file drag:

1. Build the current loop into a real `.mid` file.
2. Start an OS drag with `juce::DragAndDropContainer::performExternalDragDropOfFiles`.
3. The user drops it into Bitwig's arranger.

This reuses the existing MIDI-writing logic. Native Bitwig clip creation is not
available to a VST3 plugin and is out of scope.

### 5.2 Draggable clip preview

Add a clip-shaped `ClipDragComponent` to the editor.

- It is visually a small clip strip labelled `Drag to DAW`.
- It shows the loop length, for example `16 beats · 4 bars (4/4)`.
- It is enabled only when the current text parses successfully and the plan has
  at least one note.
- When disabled, it shows a short hint such as `Fix errors to drag`.
- Dragging starts only after a small mouse movement threshold, so a click does
  not create a file.

Starting a drag forces a parse of the current text, using the same path as
Regenerate.

- If parsing fails, no drag starts; diagnostics are shown.
- If parsing succeeds, a MIDI file is created and the external drag begins.

### 5.3 Clip contents

The drag file contains exactly one loop cycle:

- current key, mode, voicing, range, and velocity,
- current progression and durations,
- 960 ticks per quarter note,
- MIDI channel 1,
- note-offs before note-ons at equal ticks,
- a track-name event `Chord Text`,
- no tempo meta event,
- no time-signature meta event.

Omitting tempo and time signature is deliberate: the clip should follow
Bitwig's project tempo and meter. If Bitwig instead assumes 120 BPM, that is a
host-behaviour risk recorded below.

### 5.4 File location and lifetime

- Folder: `Documents\ChordText\Clips`.
- Filename: `ChordText-YYYYMMDD-HHMMSS.mid`, generated with the local time.
- The folder is created if missing.
- Files are not deleted automatically in v1; a file already referenced by a
  Bitwig project must not disappear.
- The processor retains the last created file path and exposes it to the editor
  for the Reveal action.

### 5.5 Fallback

The plugin cannot reliably detect whether Bitwig accepted a drop. Therefore:

- Every drag attempt leaves a real file in the clips folder.
- After a drag, the status line reports the saved path or filename.
- A `Reveal last clip` button is enabled when the last file exists and calls
  `File::revealToUser()`.
- If the drag could not be started, the status line reports that the clip was
  saved and the Reveal action is available.
- The user can drag the revealed file from Explorer into Bitwig.

### 5.6 Export remains unchanged

`Export .mid` keeps its current behavior:

- one loop,
- current host tempo or 120 BPM fallback,
- tempo meta event,
- 4/4 time signature,
- same notes and ordering as live playback.

The drag path uses the same MIDI event construction but omits the tempo and
time-signature events.

## 6. Architecture and components

### 6.1 Processor

`LearningRow` gains `durationBeats`.

Add a private MIDI-writing mode:

```cpp
enum class MidiFileMode { exportFile, dragClip };

bool writeMidiFile (const juce::File& file,
                    MidiFileMode mode,
                    juce::String& errorMessage);

bool exportMidiFile (const juce::File& file, juce::String& errorMessage);
```

`exportMidiFile()` delegates to `writeMidiFile(..., exportFile, ...)`.
The drag path calls `writeMidiFile(..., dragClip, ...)`.

Add message-thread methods:

```cpp
juce::File createDragClipFile (juce::String& errorMessage);
juce::File getLastDragClipFile() const;
```

`createDragClipFile()`:

- returns an empty `File` when diagnostics are non-empty or the plan has no
  notes,
- creates the clips folder,
- builds a timestamped filename,
- writes the drag-mode MIDI file,
- records the file as the last drag file,
- returns the file.

`writeMidiFile()` shares the existing event sorting and note construction.
The only difference between modes is the tempo and time-signature events.

### 6.2 Editor

Add:

- `juce::TextButton regenerateButton { "Regenerate" }`,
- a `Beats` table column,
- `ClipDragComponent clipDrag`,
- `juce::TextButton revealClipButton { "Reveal last clip" }`.

`ClipDragComponent` is a separate `Source/Plugin/ClipDragComponent.h/.cpp` unit:

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
};
```

The editor supplies `createClipFile` and updates `setLoopInfo()` from its 30 Hz
timer.

### 6.3 Data flow for a drag

```
mouse drag
  -> editor forces parse / rebuild
  -> processor.createDragClipFile()
  -> writeMidiFile(dragClip): one loop, no tempo/signature
  -> performExternalDragDropOfFiles({file}, canMoveFiles=false)
  -> status + reveal button
```

No audio-thread code changes are required.

## 7. Editor layout

The editor grows from 760×560 to 760×640 to make room for the clip preview and
the two new buttons.

- Regenerate sits on the progression header row, right-aligned.
- The Beats column is added to the table; widths are adjusted to fit.
- The clip preview sits below the table or beside the export row.
- Reveal last clip sits next to Export.

Exact positioning is settled during implementation, but the editor remains a
fixed 760×640.

## 8. Error handling and edge cases

- Invalid text: Regenerate keeps the last valid plan; drag does not start.
- Empty progression: Regenerate produces empty rows; drag is disabled.
- Missing clips folder: create it; if creation fails, report the error and do
  not start the drag.
- File write failure: report the error; do not start the drag.
- Drag start failure: keep the file; show the status and enable Reveal.
- Reveal when no file exists: button disabled.
- Very long progressions: timestamped files avoid overwrites; no automatic
  cleanup in v1.
- Host uses a non-4/4 meter: the clip is still beat-based; the preview's bar
  count is informational and assumes 4/4.
- Host tempo changes after the file is written: the file has no tempo event, so
  Bitwig's project tempo governs playback.

## 9. Testing

### Processor tests

- `LearningRow` carries the correct default and explicit duration.
- `writeMidiFile(exportFile)` still emits a tempo event and 4/4 signature.
- `writeMidiFile(dragClip)` emits no tempo or time-signature events.
- Both modes emit the same note count, pitches, ticks, channel, and
  note-off-before-note-on ordering for the same plan.
- Drag file names use the timestamp format and land under
  `Documents/ChordText/Clips`.
- `createDragClipFile` returns empty on diagnostics or an empty plan.
- `getLastDragClipFile` returns the created file.

### Editor tests

- Regenerate triggers a rebuild when the text has changed and when it has not.
- Invalid text after Regenerate leaves the previous rows and plan intact.
- The Beats column reflects default and explicit durations.
- The clip preview is disabled for invalid/empty progressions and enabled for a
  valid plan.
- Dragging an invalid progression does not create a file.
- A successful drag creates a file and enables Reveal.
- Reveal is disabled before any drag file exists.

### Manual Bitwig test

1. Open the plugin in Bitwig with a valid progression.
2. Drag the clip preview into the arranger or onto a track.
3. Confirm a MIDI clip appears with the expected notes and loop length.
4. Confirm it follows the project tempo.
5. If the drop is ignored, click Reveal last clip and drag the file from
   Explorer into Bitwig.
6. Confirm Export `.mid` still works and that exported files retain tempo and
   4/4 metadata.

## 10. Milestones

1. **Regenerate + Beats** — button, learning-row duration, table column, tests.
2. **Drag MIDI writer** — refactor the export writer into two modes, add drag
   file creation and tests.
3. **Clip preview + fallback UI** — draggable component, Reveal, status, editor
   layout.
4. **Manual Bitwig verification** — direct drag, project tempo, fallback.

## 11. Risks

- **Bitwig may reject a drag from a VST3 plugin window.** The fallback is the
  saved file plus Reveal; the feature still provides a usable workflow.
- **Bitwig may assume 120 BPM without a tempo event.** If so, a later revision
  may need a host-tempo-aware drag strategy. This version deliberately omits
  tempo so the project tempo can govern.
- **File accumulation.** Timestamped files are retained so existing Bitwig
  projects keep their source. Cleanup can be added later.
- **Editor height.** The design increases the fixed height to 640; this must be
  checked in Bitwig's plugin window.

## 12. Open questions

None. The user has approved the design direction, including the fallback
behavior and project-tempo choice.
