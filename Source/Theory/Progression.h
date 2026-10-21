#pragma once

#include "Theory/Chord.h"
#include "Theory/Diagnostic.h"
#include "Theory/Key.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordtext
{
struct ProgressionStep
{
    Chord chord;
    double durationBeats = 4.0;
    std::string sourceText;
    std::size_t offset = 0;
};

struct Progression
{
    std::vector<ProgressionStep> steps;
    double totalBeats() const;
};

struct ProgressionParseResult
{
    std::optional<Progression> progression;
    std::vector<Diagnostic> diagnostics;
};

ProgressionParseResult parseProgression (std::string_view text, const Key& key);
}
