#include "Theory/Key.h"

#include <array>
#include <cstddef>
#include <algorithm>

namespace chordtext
{
namespace
{
constexpr std::array<char, 7> letters {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
constexpr std::array<int, 7> naturalSemitones {0, 2, 4, 5, 7, 9, 11};
struct ScaleDefinition
{
    Mode mode;
    std::string_view name;
    std::string_view displayName;
    std::array<int, 7> intervals;
};

constexpr std::array<ScaleDefinition, 9> scales {{
    {Mode::major, "major", "Major (Ionian)", {0,2,4,5,7,9,11}},
    {Mode::minor, "minor", "Minor (Aeolian)", {0,2,3,5,7,8,10}},
    {Mode::dorian, "dorian", "Dorian", {0,2,3,5,7,9,10}},
    {Mode::phrygian, "phrygian", "Phrygian", {0,1,3,5,7,8,10}},
    {Mode::lydian, "lydian", "Lydian", {0,2,4,6,7,9,11}},
    {Mode::mixolydian, "mixolydian", "Mixolydian", {0,2,4,5,7,9,10}},
    {Mode::locrian, "locrian", "Locrian", {0,1,3,5,6,8,10}},
    {Mode::harmonicMinor, "harmonic-minor", "Harmonic Minor", {0,2,3,5,7,8,11}},
    {Mode::melodicMinor, "melodic-minor", "Melodic Minor", {0,2,3,5,7,9,11}}
}};

const ScaleDefinition& definition (Mode mode)
{
    const auto index = static_cast<int> (mode);
    return scales[static_cast<std::size_t> (index >= 0 && index < 9 ? index : 0)];
}

int letterIndex (char letter)
{
    for (int index = 0; index < static_cast<int> (letters.size()); ++index)
        if (letters[static_cast<std::size_t> (index)] == letter)
            return index;

    return 0;
}
}

const std::array<Mode, 9>& allModes()
{
    static const auto modes = []
    {
        std::array<Mode, 9> result {};
        for (std::size_t i = 0; i < scales.size(); ++i)
            result[i] = scales[i].mode;
        return result;
    }();
    return modes;
}

const std::array<int, 7>& modeIntervals (Mode mode) { return definition (mode).intervals; }
std::string_view modeName (Mode mode) { return definition (mode).name; }
std::string_view modeDisplayName (Mode mode) { return definition (mode).displayName; }

std::optional<Mode> parseModeName (std::string_view text)
{
    std::string lower (text);
    for (auto& character : lower)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char> (character - 'A' + 'a');
    if (lower == "ionian") return Mode::major;
    if (lower == "aeolian") return Mode::minor;
    for (const auto& scale : scales)
        if (lower == scale.name)
            return scale.mode;
    return std::nullopt;
}

Key::Key (PitchClass tonic, Mode mode)
    : tonic_ (tonic), mode_ (mode)
{
}

PitchClass Key::tonic() const
{
    return tonic_;
}

Mode Key::mode() const
{
    return mode_;
}

PitchClass Key::degree (int number) const
{
    const auto tonicIndex = letterIndex (tonic_.letter);
    const auto degreeOffset = std::clamp (number, 1, 7) - 1;
    const auto degreeIndex = (tonicIndex + degreeOffset) % static_cast<int> (letters.size());
    const auto& intervals = modeIntervals (mode_);

    auto naturalDistance = naturalSemitones[static_cast<std::size_t> (degreeIndex)]
                         - naturalSemitones[static_cast<std::size_t> (tonicIndex)];
    if (degreeIndex < tonicIndex)
        naturalDistance += 12;

    const auto alteration = tonic_.alteration
                          + intervals[static_cast<std::size_t> (degreeOffset)]
                          - naturalDistance;
    return {letters[static_cast<std::size_t> (degreeIndex)], alteration};
}

std::string Key::name() const
{
    std::string name (modeName (mode_));
    std::replace (name.begin(), name.end(), '-', ' ');
    return tonic_.name() + " " + name;
}

std::array<PitchClass, 7> Key::scaleNotes() const
{
    std::array<PitchClass, 7> notes {};
    for (int index = 0; index < 7; ++index)
        notes[static_cast<std::size_t> (index)] = degree (index + 1);
    return notes;
}
}
