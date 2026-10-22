#pragma once

#include "Cli/CliSettings.h"

#include <optional>
#include <string>
#include <string_view>

namespace chordtext::cli
{
struct CliSessionOutput
{
    std::string report;
    std::string feedback;
    bool quit = false;
};

class CliSession
{
public:
    explicit CliSession (CliSettings settings = {});
    CliSessionOutput handleLine (std::string_view line);
    const CliSettings& settings() const;
    const std::optional<std::string>& lastValidText() const;

private:
    CliSettings settings_;
    std::optional<std::string> lastValidText_;
};
}
