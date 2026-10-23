#pragma once

#include "Theory/Chord.h"
#include "Theory/Diagnostic.h"
#include <cstddef>
#include <optional>
#include <string_view>

namespace chordtext
{
struct NumericDegreeParseResult
{
    std::optional<ChordSpec> chord;
    std::optional<Diagnostic> error;
    int degree = 0;
};

NumericDegreeParseResult parseNumericDegree (std::string_view token, const Key& key,
                                             std::size_t offset);
}
