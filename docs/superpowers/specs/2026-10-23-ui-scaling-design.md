# UI scaling design

Status: approved by the user on 2026-10-23. Implemented and locally verified on 2026-10-24. Manual DAW checks remain; see the implementation plan for results.

## Purpose

Let users change the size of Chordscript to suit their screen.
Keep the current compact layout and carbon/teal colors.

## Agreed behavior

- Put a percentage dropdown at the right side of the footer.
- Use the label UI SCALE.
- Offer 75%, 100%, 125%, and 150%.
- Use 100% when there is no saved preference.
- Scale the entire plugin interface and its window together.
- Keep the current layout proportions. Do not rearrange controls for each size.
- Save one preference on this computer, outside the DAW project.
- Apply a selection to the current editor immediately.
- Keep other open editors at their current size.
- Read the saved preference each time an editor opens. This includes reopening
  an editor for an existing plugin instance.

The approved concept is Percentage dropdown in the footer. The minus/plus concept
is not part of this design.

## Sizes

The base layout is 960 by 650 logical pixels. Round each scaled dimension to the
nearest whole pixel, with half values rounded up.

| Scale | Width | Height |
|---|---|---|
| 75% | 720 | 488 |
| 100% | 960 | 650 |
| 125% | 1200 | 813 |
| 150% | 1440 | 975 |

These sizes are before the host or Windows applies display scaling. User zoom must
combine with host display scaling. It must not replace the host scale factor.

## Editor behavior

Reserve footer space for the label and dropdown. Keep status text on the left.
The new control must fit at every offered percentage. Keep keyboard and mouse
input correct after scaling. Menus, viewport scrolling, and the MIDI drag control
must use the correct coordinates.

A scale change must preserve progression text, a pending edit, caret and selection,
parameter values, validation state, and playback. It must not rebuild or replace
the musical plan. It must not close and recreate the editor.

The dropdown shows the scale applied to that editor. If another editor saves a
different preference, an already open editor must keep its current size and label.

## Local preference

Proposed storage: `%LOCALAPPDATA%\Chordscript\ui-settings.json` for the current
Windows user. Instances in different DAWs use the same file. This is a local user
preference; it does not require administrator access or apply to other Windows
accounts. This path and account scope are builder choices for review.

Store an integer `uiScalePercent`. Accept only 75, 100, 125, or 150. If the file is
missing, unreadable, or contains an invalid value, open at 100%. Reading a default
must not overwrite an existing file.

Save only when the user selects a different value. Use a temporary file and atomic
replacement so a reader cannot see a partial write. Concurrent selections use the
last successful replacement. Read the file anew when each editor opens; do not
keep a process-wide cache that can hide changes made in another DAW.

If saving fails, keep the chosen size for the current editor and show a short
status message. Other editors continue to use the last saved value. A save failure
must not change musical state or stop playback.

Keep this preference outside APVTS and plugin project state. Do not add an
automatable parameter. Loading a DAW project must not replace the local preference
or change the size of an editor that is already open.

## Implementation boundaries

Use JUCE component scaling with the existing 960 by 650 logical layout. Keep host
scaling on AudioProcessorEditor. Apply user zoom inside that boundary. The builder
may choose a content container or equivalent child transforms, provided drawing,
layout, hit testing, and resizing all use the same logical coordinates.

Put preference loading and saving in a small UI settings component. Allow tests to
provide a separate settings file. Tests must not modify the user's real preference
or depend on its value. File access and editor changes belong to the message thread.
Do not read or write preferences from the audio callback.

OS file dialogs keep their normal Windows display scaling. Keep the current
parameter IDs, parsing, voicing, MIDI output, and CLI behavior.

## Verification

- Check all four window sizes and the matching dropdown value.
- Check transformed controls remain inside the window and accept input.
- Render valid and invalid editor states at 75%, 100%, 125%, and 150%.
- Check the footer, diagnostics, Loop control, cards, and action alignment.
- Check host display scaling and user zoom combine correctly.
- Open two editors. Resize one. Confirm the other stays at its current size.
- Close and reopen the second editor. Confirm it uses the saved preference.
- Check separate settings objects using the same file see saved changes on load.
- Check missing files, invalid values, failed saves, and complete file replacement.
- Check project restore does not replace the UI preference or current editor size.
- Check scale changes preserve pending text, selection, and active MIDI playback.
- Run the full VST3 build and all three existing test suites.
- Report actual DAW resize and external drag checks separately from automated tests.

## Scope

No free window resizing, continuous zoom slider, additional percentages, automatic
screen fitting, or live resizing of other open editors.

The builder may choose helper names, file organization, concise error text, and the
exact footer spacing within the approved compact design.
