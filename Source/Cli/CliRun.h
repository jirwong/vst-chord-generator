#pragma once

#include "Cli/CliOptions.h"

#include <string>
#include <string_view>

namespace chordtext::cli
{
struct CliRunResult
{
    std::string standardOutput;
    std::string standardError;
    int exitCode = 0;
};

CliRunResult runOneShot (std::string_view text, const CliOptions& options);
}
