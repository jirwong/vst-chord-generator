#pragma once

#include "Theory/Progression.h"

#include <vector>

namespace chordtext
{
struct MidiEvent
{
    double beat = 0.0;
    int pitch = 0;
    int velocity = 100;
    int channel = 1;
    bool noteOn = true;
    int stepIndex = 0;
};

struct RenderPlan
{
    std::vector<MidiEvent> events;
    double lengthBeats = 0.0;
};

RenderPlan buildRenderPlan (const Progression& progression,
                            const std::vector<std::vector<int>>& voicings,
                            int velocity);
}
