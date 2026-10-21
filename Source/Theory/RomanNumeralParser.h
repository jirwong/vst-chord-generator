#pragma once

#include "Theory/Chord.h"
#include "Theory/Diagnostic.h"
#include "Theory/Key.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace chordtext
{
struct RomanNumeralParseResult
{
    std::optional<ChordSpec> chord;
    std::optional<Diagnostic> error;
};

RomanNumeralParseResult parseRomanNumeral (std::string_view token,
                                           const Key& key,
                                           std::size_t offset);
}
