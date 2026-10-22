#pragma once

#include "Cli/CliSettings.h"

#include <optional>
#include <string>
#include <vector>

namespace chordtext::cli
{
struct CliOptions
{
    CliSettings settings;
    std::optional<std::string> progression;
    std::optional<std::string> outputPath;
};

enum class CliParseStatus { ok, help, usageError };

struct CliParseResult
{
    CliParseStatus status = CliParseStatus::ok;
    CliOptions options;
    std::string error;
};

CliParseResult parseOptions (const std::vector<std::string>& args);
std::string helpText();
}
