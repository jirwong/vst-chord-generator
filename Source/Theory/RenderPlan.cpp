#include "Theory/RenderPlan.h"

#include <algorithm>
#include <cstddef>

namespace chordtext
{
RenderPlan buildRenderPlan (const Progression& progression,
                            const std::vector<std::vector<int>>& voicings,
                            int velocity)
{
    RenderPlan plan;

    if (voicings.size() != progression.steps.size())
        return plan;

    plan.lengthBeats = progression.totalBeats();

    const auto clampedVelocity = std::clamp (velocity, 1, 127);
    auto beat = 0.0;

    for (std::size_t index = 0; index < progression.steps.size(); ++index)
    {
        const auto& step = progression.steps[index];
        const auto stepIndex = static_cast<int> (index);

        for (const auto pitch : voicings[index])
        {
            const auto clampedPitch = std::clamp (pitch, 0, 127);
            plan.events.push_back ({beat, clampedPitch, clampedVelocity, 1, true, stepIndex});
            plan.events.push_back ({beat + step.durationBeats, clampedPitch,
                                    clampedVelocity, 1, false, stepIndex});
        }

        beat += step.durationBeats;
    }

    std::stable_sort (plan.events.begin(), plan.events.end(),
                      [] (const MidiEvent& left, const MidiEvent& right)
                      {
                          if (left.beat != right.beat)
                              return left.beat < right.beat;

                          if (left.noteOn != right.noteOn)
                              return ! left.noteOn;

                          return left.pitch < right.pitch;
                      });

    return plan;
}
}
