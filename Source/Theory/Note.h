#pragma once

#include <string>

namespace chordtext
{
struct PitchClass
{
    char letter = 'C';
    int alteration = 0;

    int semitone() const;
    std::string name() const;
};

struct Note
{
    PitchClass pitchClass;
    int octave = 4;

    int midi() const;
    std::string name() const;
};
}
