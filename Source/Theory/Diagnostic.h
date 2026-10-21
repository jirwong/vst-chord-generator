#pragma once

#include <cstddef>
#include <string>

namespace chordtext
{
struct Diagnostic
{
    std::string token;
    std::size_t offset = 0;
    std::string message;
};
}
