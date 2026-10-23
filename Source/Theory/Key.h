#pragma once

#include "Theory/Note.h"

#include <string>
#include <array>
#include <optional>
#include <string_view>

namespace chordtext
{
enum class Mode
{
    major = 0,
    minor = 1,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    harmonicMinor,
    melodicMinor
};

const std::array<Mode, 9>& allModes();
const std::array<int, 7>& modeIntervals (Mode mode);
std::string_view modeName (Mode mode);
std::string_view modeDisplayName (Mode mode);
std::optional<Mode> parseModeName (std::string_view text);

class Key
{
public:
    Key (PitchClass tonic, Mode mode);

    PitchClass tonic() const;
    Mode mode() const;
    PitchClass degree (int number) const;
    std::array<PitchClass, 7> scaleNotes() const;
    std::string name() const;

private:
    PitchClass tonic_;
    Mode mode_;
};
}
