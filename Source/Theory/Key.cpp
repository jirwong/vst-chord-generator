#include "Theory/Key.h"

#include <array>
#include <cstddef>

namespace chordtext
{
namespace
{
constexpr std::array<char, 7> letters {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
constexpr std::array<int, 7> naturalSemitones {0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> majorIntervals {0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> minorIntervals {0, 2, 3, 5, 7, 8, 10};

int letterIndex (char letter)
{
    for (int index = 0; index < static_cast<int> (letters.size()); ++index)
        if (letters[static_cast<std::size_t> (index)] == letter)
            return index;

    return 0;
}
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
    const auto degreeOffset = number - 1;
    const auto degreeIndex = (tonicIndex + degreeOffset) % static_cast<int> (letters.size());
    const auto& intervals = mode_ == Mode::major ? majorIntervals : minorIntervals;

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
    return tonic_.name() + (mode_ == Mode::major ? " major" : " minor");
}
}
