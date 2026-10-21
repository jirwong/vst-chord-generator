#pragma once

#include "Theory/Note.h"

#include <string>

namespace chordtext
{
enum class Mode
{
    major,
    minor
};

class Key
{
public:
    Key (PitchClass tonic, Mode mode);

    PitchClass tonic() const;
    Mode mode() const;
    PitchClass degree (int number) const;
    std::string name() const;

private:
    PitchClass tonic_;
    Mode mode_;
};
}
