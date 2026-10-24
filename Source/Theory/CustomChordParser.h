#pragma once

#include "Theory/Chord.h"
#include "Theory/Diagnostic.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordtext
{
struct CustomChordParseResult
{
    std::optional<ChordSpec> spec;
    std::vector<Diagnostic> diagnostics;
};

CustomChordParseResult parseCustomChord (std::string_view text,
                                         const Key& key,
                                         std::size_t offset);

std::string voicedNoteName (const PitchClass& spelling, int midi);
}
