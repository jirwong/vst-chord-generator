#pragma once

#include "Theory/Chord.h"
#include "Theory/Diagnostic.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace chordtext
{
struct ChordSymbolParseResult
{
    std::optional<ChordSpec> chord;
    std::optional<Diagnostic> error;
};

ChordSymbolParseResult parseChordSymbol (std::string_view token, std::size_t offset);

struct QualityMatch
{
    std::string_view canonical;
    std::size_t length = 0;
};

// Returns the longest supported quality token matching text at position.
QualityMatch longestQuality (std::string_view text, std::size_t position);
}
