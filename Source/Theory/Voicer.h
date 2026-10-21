#pragma once

#include "Theory/Chord.h"
#include "Theory/Progression.h"

#include <optional>
#include <vector>

namespace chordtext
{
enum class VoicingMode
{
    voiceLed,
    rootPosition
};

struct VoicingSettings
{
    VoicingMode mode = VoicingMode::voiceLed;
    int lowMidi = 48;
    int highMidi = 84;
};

std::vector<int> voiceChord (const Chord& chord,
                             const std::optional<std::vector<int>>& previous,
                             const VoicingSettings& settings);

std::vector<std::vector<int>> voiceProgression (const Progression& progression,
                                                const VoicingSettings& settings);
}
