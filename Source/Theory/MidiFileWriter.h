#pragma once

#include "Theory/RenderPlan.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace chordtext
{
struct MidiFileSettings
{
    double bpm = 120.0;
    bool includeTempoAndTimeSignature = true;
    std::string trackName = "Chord Text";
    int ticksPerQuarterNote = 960;
};

std::vector<std::uint8_t> serialiseMidiFile (const RenderPlan& plan,
                                             const MidiFileSettings& settings);

bool writeMidiFile (const RenderPlan& plan,
                    const std::filesystem::path& file,
                    const MidiFileSettings& settings,
                    std::string& error);
}
