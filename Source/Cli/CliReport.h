#pragma once

#include "Cli/CliSettings.h"
#include "Theory/Diagnostic.h"
#include "Theory/RenderPlan.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordtext::cli
{
struct CliChordRow
{
    std::string source;
    std::string symbol;
    std::string degree;
    std::vector<std::string> spelledTones;
    std::vector<std::string> notes;
    std::vector<int> voicing;
    std::optional<int> bassMidi;
    std::optional<std::string> bassNote;
    double startBeat = 0.0;
    double durationBeats = 4.0;
    std::string lengthExpression;
    bool usesDefaultLength = true;
};

struct CliReport
{
    std::string text;
    CliSettings settings;
    bool valid = false;
    std::vector<Diagnostic> diagnostics;
    std::vector<CliChordRow> chords;
    RenderPlan plan;
    std::optional<std::string> midiFile;
    std::optional<std::string> error;
};

CliReport buildReport (std::string_view text, const CliSettings& settings);
std::string formatHuman (const CliReport& report);
std::string formatJson (const CliReport& report);
}
