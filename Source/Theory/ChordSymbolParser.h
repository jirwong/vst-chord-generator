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
}
