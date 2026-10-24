#pragma once

#include "Theory/Diagnostic.h"
#include "Theory/Progression.h"
#include "Theory/Voicer.h"

#include <optional>
#include <vector>

namespace chordtext
{
struct ResolvedChord
{
    std::vector<int> chordPitches;
    std::vector<int> pitches;
    int rootMidi = 0;
    std::optional<int> bassMidi;
};

struct ChordResolutionResult
{
    std::optional<std::vector<ResolvedChord>> chords;
    std::vector<Diagnostic> diagnostics;
};

ChordResolutionResult resolveProgression (const Progression& progression,
                                          const VoicingSettings& settings,
                                          int bassOctaves);
}
