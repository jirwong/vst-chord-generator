#include "Theory/Voicer.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace chordtext
{
namespace
{
struct Range
{
    int low = 48;
    int high = 84;
};

Range normaliseRange (const VoicingSettings& settings)
{
    Range range;
    range.low = std::clamp (settings.lowMidi, 0, 127);
    range.high = std::clamp (settings.highMidi, 0, 127);

    if (range.low > range.high)
        std::swap (range.low, range.high);

    return range;
}

int pitchClassOf (int midi)
{
    return ((midi % 12) + 12) % 12;
}

int placementAtOrAbove (int pitchClass, int floorPitch, int highPitch)
{
    const auto pitch = floorPitch + ((pitchClass - floorPitch) % 12 + 12) % 12;
    return pitch <= highPitch ? pitch : -1;
}

int placementAtOrBelow (int pitchClass, int ceilingPitch)
{
    return ceilingPitch - ((ceilingPitch - pitchClass) % 12 + 12) % 12;
}

int bassPitchFor (const Chord& chord, const Range& range)
{
    const auto& spec = chord.spec();
    const auto pc = pitchClassOf (spec.slashBass.has_value() ? spec.slashBass->semitone()
                                                             : spec.root.semitone());
    auto bass = placementAtOrAbove (pc, range.low, range.high);

    if (bass < 0)
    {
        bass = placementAtOrBelow (pc, range.high);

        if (bass < range.low)
            bass = range.low;
    }

    return bass;
}

std::vector<int> stackTonesAbove (const Chord& chord, int floorPitch,
                                  std::size_t firstTone, const Range& range)
{
    std::vector<int> pitches;
    const auto& tones = chord.tones();

    for (auto index = firstTone; index < tones.size(); ++index)
    {
        const auto pc = pitchClassOf (tones[index].semitone());
        auto pitch = placementAtOrAbove (pc, floorPitch, range.high);

        if (pitch < 0)
        {
            // The chord is wider than the range. Keep the tone in range by
            // clamping it to the nearest valid octave; the caller's range
            // guarantee matters more than a complete stack here.
            pitch = placementAtOrBelow (pc, range.high);

            if (pitch < range.low)
                pitch = range.low;
        }

        pitches.push_back (pitch);
        floorPitch = pitch + 1;
    }

    return pitches;
}

std::vector<int> rootPositionVoicing (const Chord& chord, const Range& range)
{
    const auto bass = bassPitchFor (chord, range);
    const auto firstTone = chord.spec().slashBass.has_value() ? std::size_t {0} : std::size_t {1};
    const auto upper = stackTonesAbove (chord, bass + 1, firstTone, range);

    std::vector<int> pitches;
    pitches.reserve (upper.size() + 1);
    pitches.push_back (bass);
    pitches.insert (pitches.end(), upper.begin(), upper.end());
    return pitches;
}

std::vector<std::vector<int>> buildCandidates (const Chord& chord, const Range& range)
{
    const auto hasSlashBass = chord.spec().slashBass.has_value();
    const auto bass = bassPitchFor (chord, range);

    // The anchor is the root-position stack. Inversions are formed by starting
    // on each successive tone and stacking the rest above it; octave placements
    // raise that start by whole octaves. Rotations are never transposed below
    // the anchor, which keeps the voicings registered around the root position.
    const auto base = hasSlashBass ? stackTonesAbove (chord, bass + 1, 0, range)
                                   : rootPositionVoicing (chord, range);
    const auto count = base.size();

    std::vector<std::vector<int>> candidates;

    if (count == 0)
        return candidates;

    for (std::size_t rotation = 0; rotation < count; ++rotation)
    {
        for (auto start = base[rotation]; start <= range.high; start += 12)
        {
            std::vector<int> candidate;
            candidate.reserve (count);
            candidate.push_back (start);

            auto previousPitch = start;
            auto fits = true;

            for (std::size_t step = 1; step < count; ++step)
            {
                const auto toneIndex = (rotation + step) % count;
                const auto pc = pitchClassOf (base[toneIndex]);
                const auto pitch = placementAtOrAbove (pc, previousPitch + 1, range.high);

                if (pitch < 0)
                {
                    fits = false;
                    break;
                }

                candidate.push_back (pitch);
                previousPitch = pitch;
            }

            if (! fits)
                continue;

            if (hasSlashBass)
                candidate.insert (candidate.begin(), bass);

            if (std::find (candidates.begin(), candidates.end(), candidate) == candidates.end())
                candidates.push_back (std::move (candidate));
        }
    }

    return candidates;
}

struct MovementScore
{
    int total = 0;
    int maximum = 0;
    double averagePitch = 0.0;
};

MovementScore scoreMovement (const std::vector<int>& candidate,
                             const std::vector<int>& previous)
{
    auto sortedPrevious = previous;
    std::sort (sortedPrevious.begin(), sortedPrevious.end());

    const auto paired = std::min (candidate.size(), sortedPrevious.size());
    auto total = 0;
    auto maximum = 0;

    for (std::size_t index = 0; index < paired; ++index)
    {
        const auto distance = std::abs (candidate[index] - sortedPrevious[index]);
        total += distance;
        maximum = std::max (maximum, distance);
    }

    auto pitchSum = 0;
    for (const auto pitch : candidate)
        pitchSum += pitch;

    const auto average = candidate.empty() ? 0.0
                                           : static_cast<double> (pitchSum)
                                               / static_cast<double> (candidate.size());

    return {total, maximum, average};
}

bool isBetter (const MovementScore& candidate, const MovementScore& current)
{
    if (candidate.total != current.total)
        return candidate.total < current.total;

    if (candidate.maximum != current.maximum)
        return candidate.maximum < current.maximum;

    return candidate.averagePitch < current.averagePitch;
}
}

std::vector<int> voiceChord (const Chord& chord,
                             const std::optional<std::vector<int>>& previous,
                             const VoicingSettings& settings)
{
    const auto range = normaliseRange (settings);
    const auto rootPosition = rootPositionVoicing (chord, range);

    if (settings.mode == VoicingMode::rootPosition
        || ! previous.has_value()
        || previous->empty())
        return rootPosition;

    const auto candidates = buildCandidates (chord, range);

    if (candidates.empty())
        return rootPosition;

    const auto* best = &candidates.front();
    auto bestScore = scoreMovement (*best, *previous);

    for (std::size_t index = 1; index < candidates.size(); ++index)
    {
        const auto score = scoreMovement (candidates[index], *previous);

        if (isBetter (score, bestScore))
        {
            best = &candidates[index];
            bestScore = score;
        }
    }

    return *best;
}

std::vector<std::vector<int>> voiceProgression (const Progression& progression,
                                                const VoicingSettings& settings)
{
    std::vector<std::vector<int>> voicings;
    voicings.reserve (progression.steps.size());

    std::optional<std::vector<int>> previous;

    for (const auto& step : progression.steps)
    {
        auto voicing = voiceChord (step.chord, previous, settings);
        previous = voicing;
        voicings.push_back (std::move (voicing));
    }

    return voicings;
}
}
