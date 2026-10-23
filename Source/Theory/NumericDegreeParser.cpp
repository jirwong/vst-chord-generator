#include "Theory/NumericDegreeParser.h"

#include <string>
#include <utility>

namespace chordtext
{
namespace
{
NumericDegreeParseResult failure (std::string_view token, std::size_t offset,
                                  std::string message)
{
    return {std::nullopt, Diagnostic {std::string (token), offset, std::move (message)}, 0};
}

struct QualityOverride { std::string_view text; std::string_view quality; bool raisedFifth = false; };
constexpr QualityOverride overrides[] {
    {"maj", ""}, {"m", "m"}, {"min", "m"}, {"dim", "dim"}, {"o", "dim"}, {"°", "dim"},
    {"aug", "aug"}, {"+", "aug"}, {"maj7", "maj7"}, {"m7", "m7"}, {"min7", "m7"},
    {"dom7", "7"}, {"mMaj7", "mMaj7"}, {"minMaj7", "mMaj7"}, {"dim7", "dim7"},
    {"o7", "dim7"}, {"°7", "dim7"}, {"m7b5", "m7b5"}, {"ø7", "m7b5"},
    {"augMaj7", "maj7", true}
};
}

NumericDegreeParseResult parseNumericDegree (std::string_view token, const Key& key,
                                             std::size_t offset)
{
    if (token.empty() || token.front() < '1' || token.front() > '7')
        return failure (token, offset, "numeric degree must be a number from 1 to 7; root accidentals are not supported");
    if (token.size() > 1 && token[1] >= '0' && token[1] <= '9')
        return failure (token, offset, "separate numeric degrees with spaces or commas; use (7) for a scale seventh");

    const auto degree = token.front() - '0';
    const auto suffix = token.substr (1);
    ChordSpec spec {key.degree (degree), "", {}, std::nullopt, std::string (token)};
    if (suffix.empty() || suffix == "(7)")
    {
        const auto& scale = modeIntervals (key.mode());
        const auto rootIndex = degree - 1;
        const auto interval = [&] (int offsetInScale)
        {
            const auto index = rootIndex + offsetInScale;
            return scale[static_cast<std::size_t> (index % 7)] + 12 * (index / 7)
                 - scale[static_cast<std::size_t> (rootIndex)];
        };
        const auto third = interval (2);
        const auto fifth = interval (4);
        if (third == 3 && fifth == 7) spec.quality = "m";
        else if (third == 3 && fifth == 6) spec.quality = "dim";
        else if (third == 4 && fifth == 8) spec.quality = "aug";
        else if (third != 4 || fifth != 7)
            return failure (token, offset, "unsupported scale triad");

        if (suffix == "(7)")
        {
            const auto seventh = interval (6);
            if (spec.quality.empty() && seventh == 10) spec.quality = "7";
            else if (spec.quality.empty() && seventh == 11) spec.quality = "maj7";
            else if (spec.quality == "m" && seventh == 10) spec.quality = "m7";
            else if (spec.quality == "m" && seventh == 11) spec.quality = "mMaj7";
            else if (spec.quality == "dim" && seventh == 9) spec.quality = "dim7";
            else if (spec.quality == "dim" && seventh == 10) spec.quality = "m7b5";
            else if (spec.quality == "aug" && seventh == 11)
            {
                spec.quality = "maj7";
                spec.alterations.emplace_back (5, 1);
            }
            else return failure (token, offset, "unsupported scale seventh");
        }
    }
    else
    {
        bool matched = false;
        for (const auto& quality : overrides)
            if (suffix == quality.text)
            {
                spec.quality = quality.quality;
                if (quality.raisedFifth) spec.alterations.emplace_back (5, 1);
                matched = true;
                break;
            }
        if (! matched)
            return failure (token, offset, "unknown numeric chord suffix; use (7) or an explicit triad/seventh quality");
    }
    return {std::move (spec), std::nullopt, degree};
}
}
