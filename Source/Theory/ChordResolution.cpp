#include "Theory/ChordResolution.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace chordtext
{
namespace
{
int normalisedPitchClass (const PitchClass& pitchClass)
{
    return (pitchClass.semitone() % 12 + 12) % 12;
}

Diagnostic makeDiagnostic (const ProgressionStep& step, std::size_t offset,
                           std::string message)
{
    const auto token = step.sourceText.empty() ? step.chord.spec().sourceText : step.sourceText;
    return {token, offset, std::move (message)};
}

bool pitchIsValid (int pitch)
{
    return pitch >= 0 && pitch <= 127;
}

std::optional<int> baselinePitchFor (const std::vector<int>& baseline,
                                     const PitchClass& requested)
{
    for (const auto pitch : baseline)
        if ((pitch % 12 + 12) % 12 == normalisedPitchClass (requested))
            return pitch;
    return std::nullopt;
}

std::optional<int> ordinaryRootPitch (const Chord& chord, const std::vector<int>& baseline)
{
    const auto root = normalisedPitchClass (chord.spec().root);
    std::optional<int> result;
    for (const auto pitch : baseline)
        if ((pitch % 12 + 12) % 12 == root
            && (! result.has_value() || pitch < *result))
            result = pitch;
    return result;
}
}

ChordResolutionResult resolveProgression (const Progression& progression,
                                          const VoicingSettings& settings,
                                          int bassOctaves)
{
    ChordResolutionResult result;
    if (bassOctaves < 0 || bassOctaves > 3)
    {
        result.diagnostics.push_back ({ {}, 0, "bass octave offset must be from 0 to 3" });
        return result;
    }

    std::vector<ResolvedChord> resolved;
    resolved.reserve (progression.steps.size());
    std::optional<std::vector<int>> previousBaseline;

    for (const auto& step : progression.steps)
    {
        const auto baseline = voiceChord (step.chord, previousBaseline, settings);
        previousBaseline = baseline;

        ResolvedChord chord;
        const auto& customEntries = step.chord.spec().customNotes;
        if (customEntries.has_value())
        {
            if (customEntries->empty())
            {
                result.diagnostics.push_back (makeDiagnostic (
                    step, step.offset, "custom chord needs at least one note"));
                return result;
            }

            std::vector<std::pair<int, PitchClass>> customPitches;
            customPitches.reserve (customEntries->size());
            for (const auto& entry : *customEntries)
            {
                const auto baselinePitch = baselinePitchFor (baseline, entry.pitchClass);
                if (! entry.exactMidi.has_value() && ! baselinePitch.has_value())
                {
                    result.diagnostics.push_back (makeDiagnostic (
                        step, entry.offset, "no baseline voice is available for " + entry.sourceText));
                    return result;
                }

                const auto pitch = entry.exactMidi.has_value()
                    ? *entry.exactMidi : *baselinePitch + 12 * entry.octaveOffset;
                if (! pitchIsValid (pitch))
                {
                    result.diagnostics.push_back (makeDiagnostic (
                        step, entry.offset, "resolved note " + entry.sourceText
                            + " is outside MIDI pitch range 0 to 127"));
                    return result;
                }

                customPitches.emplace_back (pitch, entry.pitchClass);
                if (&entry == customEntries->data())
                    chord.rootMidi = pitch;
            }

            std::stable_sort (customPitches.begin(), customPitches.end(),
                              [] (const auto& left, const auto& right)
                              {
                                  return left.first < right.first;
                              });
            for (const auto& note : customPitches)
            {
                if (chord.chordPitches.empty() || chord.chordPitches.back() != note.first)
                {
                    chord.chordPitches.push_back (note.first);
                    chord.chordSpellings.push_back (note.second);
                }
            }
        }
        else
        {
            chord.chordPitches = baseline;
            std::sort (chord.chordPitches.begin(), chord.chordPitches.end());
            chord.chordPitches.erase (
                std::unique (chord.chordPitches.begin(), chord.chordPitches.end()),
                chord.chordPitches.end());
            const auto root = ordinaryRootPitch (step.chord, baseline);
            if (! root.has_value())
            {
                result.diagnostics.push_back (makeDiagnostic (
                    step, step.offset, "no voiced note is available for the chord root"));
                return result;
            }
            chord.rootMidi = *root;
        }

        chord.pitches = chord.chordPitches;

        if (bassOctaves > 0)
        {
            const auto bass = chord.rootMidi - 12 * bassOctaves;
            if (! pitchIsValid (bass))
            {
                const auto offset = customEntries.has_value() && ! customEntries->empty()
                    ? customEntries->front().offset : step.offset;
                result.diagnostics.push_back (makeDiagnostic (
                    step, offset, "added root bass is outside MIDI pitch range 0 to 127"));
                return result;
            }

            chord.bassMidi = bass;
            chord.pitches.push_back (bass);
            std::sort (chord.pitches.begin(), chord.pitches.end());
            chord.pitches.erase (std::unique (chord.pitches.begin(), chord.pitches.end()),
                                 chord.pitches.end());
        }

        resolved.push_back (std::move (chord));
    }

    result.chords = std::move (resolved);
    return result;
}
}
