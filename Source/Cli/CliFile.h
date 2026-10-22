#pragma once

#include <filesystem>
#include <string_view>

namespace chordtext::cli
{
std::filesystem::path resolveMidiPath (std::string_view userPath);
}
