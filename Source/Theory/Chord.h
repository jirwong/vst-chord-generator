#pragma once

#include "Theory/Key.h"
#include "Theory/Note.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chordtext
{
struct ChordSpec
{
    PitchClass root;
    std::string quality;
    std::vector<std::pair<int, int>> alterations;
    std::optional<PitchClass> slashBass;
    std::string sourceText;
};

class Chord
{
public:
    explicit Chord (ChordSpec spec);

    const ChordSpec& spec() const;
    const std::vector<PitchClass>& tones() const;
    std::string symbol() const;

private:
    ChordSpec spec_;
    std::vector<PitchClass> tones_;
};

std::string degreeLabel (const Chord& chord, const Key& key);
std::string noteNameForPitch (const Chord& chord, int midi);
std::string notesForVoicing (const Chord& chord, const std::vector<int>& voicing);
}
