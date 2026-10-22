#include "Cli/CliFile.h"

#include <cctype>
#include <string>

namespace chordtext::cli
{
std::filesystem::path resolveMidiPath (std::string_view userPath)
{
    auto path = std::filesystem::u8path (std::string (userPath));
    if (path.is_relative())
        path = std::filesystem::absolute (path);

    const auto extension = path.extension().u8string();

    std::string lower;
    lower.reserve (extension.size());
    for (const auto c : extension)
        lower.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (c))));

    if (lower != ".mid")
        path.replace_extension (".mid");

    return path;
}
}
