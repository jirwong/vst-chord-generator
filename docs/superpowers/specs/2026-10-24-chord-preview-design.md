# Chord card preview design

Status: implemented and verified on 2026-10-24 on branch `codex/chord-preview`,
opened for review. Merge and installation still require a separate user request.
Automated verification: Release VST3 built; TheoryTests 531/531, ProcessorTests
0 failures, CliTests 262/262. Independent review findings were resolved. Actual
DAW/host checks remain unperformed and are documented in README.md.

## Purpose

Let users hear a chord directly from its sequence card. They can compare chords without changing the progression or starting the DAW transport.

Chordscript sends MIDI to the user's connected instrument. This feature does not add a synthesizer or audio output.

## Decisions approved by the user

- Left-press a chord card to play its chord. Release the left mouse button to stop the preview.
- Right-click a card to hold its chord. Right-click that same held card again to stop it.
- Both actions work with the DAW transport stopped.
- Only one manual preview chord can sound per plugin instance.
- Selecting another chord replaces the previous preview. Do not layer the chords.
- Left-pressing another card replaces a held chord. Releasing the button does not restore the old hold.
- During DAW playback, manual preview replaces the progression's MIDI output. The DAW timeline continues moving.
- When preview ends, resume the progression at its current DAW position. Do not restart the progression.
- Stop preview when the progression or musical settings change, or when the editor closes.
- Preserve the compact carbon/teal design. Use explicit PREVIEW, HELD, and PLAYING card labels. The held state also has a small monochrome pin icon.
- Finish the spec and implementation plan here. Hand implementation to another agent after approval.

## Interaction rules

The following details are design calls proposed by Codex. They make the approved behavior precise and are included in this approval request.

| Current state | Input | Result |
|---|---|---|
| No manual preview | Left-down on a valid card | Play that chord as a momentary preview |
| Momentary preview | Matching left-up | Release preview; resume eligible progression playback |
| Any manual preview | Left-down on any valid card | Release the old chord and start that card as a new momentary preview |
| No manual preview or momentary preview | Right-down on a valid card | Release any old preview and hold that card |
| Held card | Right-down on the same card | Release preview; resume eligible progression playback |
| Held card | Right-down on another card | Release the old chord and hold the new chord |
| Any manual preview | Progression/settings change, editor close, bypass, or processing reset | Cancel preview and release its notes through the audio callback |

- Left-pressing the same held card changes it to a momentary preview. Release and retrigger its notes, then stop it on left-up. Do not restore the hold.
- Trigger left preview on mouse-down, not on a completed click. Toggle hold on right mouse-down. Suppress the card's native context menu.
- Moving outside the card does not choose another chord. A matching release outside the card or plugin still ends the momentary preview. Loss of mouse capture also cancels a momentary preview.
- A stale mouse-up must not cancel a newer right-held chord or a replacement preview. Associate releases with their matching gesture.
- Ordinary focus loss does not cancel a right-held chord. The user can interact with the DAW while holding it. The lifecycle cancellation rules still apply.
- A double-click has no separate action. Each press follows the rules above. Touch, keyboard audition, and hover/glissando are outside this feature.

## Preview eligibility and cancellation

- Enable preview only for current, valid, non-empty progression cards.
- Disable it while text edits are pending and while diagnostics are invalid. Stale learning cards remain visible under the existing rules but cannot start preview.
- Cancel manual preview as soon as a text edit begins, before the normal debounce completes. Keep the existing last-valid playback-plan behavior.
- Cancel when key, either mode parameter, voicing, range, velocity, or Loop changes. Also cancel on project-state restore, editor close, plugin bypass, prepareToPlay, or releaseResources.
- UI scale changes, exporting MIDI, and dragging a clip do not cancel preview. The feature must preserve the existing UI scaling behavior.
- A canceled hold does not restart automatically after a valid edit, unbypass, editor reopen, project load, or processing reset. A new gesture is required.
- Transport start, stop, tempo changes, looping, and seeking do not cancel or retrigger a manual preview. It remains under the user's gesture or latch. When it ends, use the host's current position if normal playback is available.

## MIDI behavior

- Use the selected card's actual generated pitches, channel 1, and current velocity. Preserve the voicer's range and chord extensions. Do not derive MIDI from displayed note strings or reimplement theory logic.
- Ignore the card's beat duration during manual preview. Gesture/latch state determines duration.
- Emit preview transitions at sample 0 of the next available processing block. Release the previous output before starting the replacement, including notes shared by both chords.
- Codex timing call: each block applies the latest desired preview state. A press and release completed before that block sends no preview note-on. A series of replacements before that block starts only the final requested chord. Do not replay completed gestures late. If a new gesture requests the same chord that is already sounding, distinguish its gesture identity and release/retrigger it as specified above.
- While preview is active, suppress normal progression note events. Do not pause or modify the host timeline, progression text, loop setting, or export plan.
- On release while the DAW is playing, resynchronize the normal scheduler to the current PPQ position. Sound the chord that should be active there and schedule the remaining events in that block. Avoid duplicate note-ons at an exact chord boundary.
- If normal playback is not eligible, release preview to silence. Examples include stopped transport, unavailable PPQ/tempo, an empty plan, or a completed non-looping progression.
- Manual preview does not require a host playhead, PPQ, or tempo. It does require the host to call the processor so MIDI can be delivered. Host suspension must not cause old preview requests to replay on a later processing reset.
- Do not emit a new note-on merely because another audio block arrives. Track and release all preview pitches. Preserve note-offs before note-ons at equal sample offsets.
- Release only this processor's tracked notes. Do not send a channel-wide all-notes-off command to silence unrelated sources.

## UI states

Use the approved interactive mockup as the visual reference:

`C:\Users\jirwo\.codex\visualizations\2026\10\22\01a165c6-3b92-7423-8304-f20cee5b321a\chordscript-chord-preview.html`

- Idle cards keep their current appearance and chord information.
- The sounding manual card gets the existing teal outline, subtle fill, and top marker.
- A left-held card shows PREVIEW. A right-held card shows a pin icon and HELD.
- When manual preview is active, remove the normal PLAYING indication from other cards. Only the sounding source receives that highlight.
- When normal progression output resumes, its current card shows PLAYING with the existing active style. Do not show PLAYING when transport/output is unavailable.
- Add a compact hint near the chord sequence: `Left press to preview · Right-click to hold`.
- Show concise preview status, such as `Dm7 held · Right-click again to release`. Keep transport status tied to the host; a manual preview must not imply that transport is playing.
- Retain error placement, collapsed valid diagnostics, action heights, Loop margin, and all four UI scales. No new permanent stop button or extra settings panel is included.
- Reflect accepted processor state and clear obsolete UI feedback on cancellation. Do not leave a HELD badge after the audio side rejects or cancels its request.

## Architecture constraints

Extend the current C++17/JUCE architecture. Keep theory and CLI JUCE-free.

- The message thread resolves card identity against the current successful progression/voicing result. Use an immutable pitch snapshot or equivalent data from the shared pipeline.
- Transfer desired preview state safely to the audio thread. Bound payload size and work. No locks, allocations, parsing, file access, GUI calls, or plan rebuilds in processBlock.
- Preserve the existing triple-buffer render-plan ownership. A UI gesture must not call rebuildPlan or publishPlan merely to audition a chord.
- Include revision/gesture ownership so old card requests, delayed releases, and canceled requests cannot revive an obsolete chord. Cancellation must not depend on obtaining space in a full event queue.
- Keep manual MIDI handling independent from the normal scheduler's missing/stopped transport early returns. Preview must work on those paths while normal generation remains host-driven.
- Track note ownership consistently across preview, progression, bypass, prepareToPlay, and releaseResources. The latter cannot emit MIDI; retain the documented host-flush limitation. prepareToPlay must queue releases for sample 0 of the next block.
- Preview is transient and per processor instance. Do not add APVTS parameters or serialize the preview/latch in project state or global UI preferences. Preserve existing parameter IDs and state migration.
- Export and drag continue to represent the full progression. Preview gestures must not change their bytes or enabled state, apart from existing pending/invalid-input rules.

The implementer may choose helper types, exact thread-transfer mechanics, and internal API names. The plan must specify ownership, cancellation, and resynchronization clearly before implementation starts.

## Verification requirements

Use isolated processor/editor fixtures and the existing CHECK suites. Include:

- Left-down/up and right-toggle, replacement, same-card retrigger, shared-pitch off/on ordering, stale releases, and multiple gestures before the next processing block.
- Stopped transport and missing playhead/PPQ/tempo. Manual notes must still work while processing continues.
- Active progression override, no background notes or repeated preview note-ons, resuming mid-chord and at exact boundaries, seeks, tempo changes, loop wrap, and the end of a non-looping progression.
- Text changes before debounce, invalid/empty input, all musical parameter changes, state restore, editor close, bypass, and processing reset. No canceled latch may reappear.
- Captured release outside the card, card gaps, scrolled card hit testing, and all four UI scales. UI labels must match the sounding source.
- Project-state and export/drag invariance, independent plugin instances, and unchanged behavior when no preview is requested.
- An independent review of the design and implementation plan before the handoff. The implementation agent must review product changes before completion.

For implementation verification, follow AGENTS.md: build the Release VST3 and run TheoryTests, ProcessorTests, and CliTests. Report actual counts, compiler warnings, and git diff --check. Inspect renders of idle, momentary, held, normal playback, pending, and invalid states.

Actual DAW checks must cover routed-instrument sound with transport stopped and playing, note release outside the window, latch cancellation, host bypass/reset, scaling and scrolling, and return to the current timeline position. Distinguish automated checks from unperformed host checks. No new tests or builds are needed during this design-only phase.

## Handoff

After the user approves this spec, prepare the implementation plan with the writing-plans workflow. After the plan is approved, assign implementation to another agent as requested. Provide the approved documents, repository guidance, current branch/base, verification expectations, and the boundary that no PR or merge is authorized by this design approval alone.

The current checkout is clean on main at 39496e6 before this document was written. Preserve unrelated concurrent work. The implementation agent must inspect live Git state before choosing an isolated feature branch or checkout.

## Builder check

A fresh JUCE reviewer checked the design against the current repository. The only open question was whether several gestures before one audio block require every intermediate transition. Codex settled this with the latest-desired-state timing rule above, presented as a design call for user approval. No other important planning questions remained. The implementation plan will choose the concrete safe state-transfer mechanism.
