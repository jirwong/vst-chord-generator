#include "Theory/Note.h"

#include <string>

namespace chordtext
{
int PitchClass::semitone() const
{
    int naturalSemitone = 0;
    switch (letter)
    {
        case 'C': naturalSemitone = 0; break;
        case 'D': naturalSemitone = 2; break;
        case 'E': naturalSemitone = 4; break;
        case 'F': naturalSemitone = 5; break;
        case 'G': naturalSemitone = 7; break;
        case 'A': naturalSemitone = 9; break;
        case 'B': naturalSemitone = 11; break;
        default: break;
    }

    return naturalSemitone + alteration;
}

std::string PitchClass::name() const
{
    std::string result (1, letter);
    for (int count = 0; count < alteration; ++count)
        result += '#';
    for (int count = 0; count > alteration; --count)
        result += 'b';
    return result;
}

int Note::midi() const
{
    return 12 * (octave + 1) + pitchClass.semitone();
}

std::string Note::name() const
{
    return pitchClass.name() + std::to_string (octave);
}
}
