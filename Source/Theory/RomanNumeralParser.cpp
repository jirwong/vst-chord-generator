#include "Theory/RomanNumeralParser.h"

#include "Theory/ChordSymbolParser.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chordtext
{
namespace
{
struct NumeralMatch
{
    int degree = 0;
    std::size_t length = 0;
    bool lowercase = false;
};

constexpr std::array<std::string_view, 7> numerals {"VII", "VI", "V", "IV", "III", "II", "I"};

bool isAccidental (char character)
{
    return character == '#' || character == 'b';
}

NumeralMatch longestNumeral (std::string_view text, std::size_t position)
{
    NumeralMatch best;

    for (std::size_t index = 0; index < numerals.size(); ++index)
    {
        const auto numeral = numerals[index];

        if (numeral.size() <= best.length || numeral.size() > text.size() - position)
            continue;

        bool matches = true;

        for (std::size_t offset = 0; offset < numeral.size(); ++offset)
        {
            const auto character = static_cast<char> (
                std::toupper (static_cast<unsigned char> (text[position + offset])));

            if (character != numeral[offset])
            {
                matches = false;
                break;
            }
        }

        if (matches)
            best = { static_cast<int> (numerals.size() - index), numeral.size(),
                     std::islower (static_cast<unsigned char> (text[position])) != 0 };
    }

    return best;
}

std::string resolveQuality (bool lowercase, const QualityMatch& quality)
{
    if (quality.length == 0)
        return lowercase ? "m" : "";

    std::string result (quality.canonical);

    // A bare extension keeps the triad mode implied by the numeral's case.
    if (lowercase)
    {
        if (result == "6")
            result = "m6";
        else if (result == "7")
            result = "m7";
        else if (result == "9")
            result = "m9";
    }

    return result;
}

RomanNumeralParseResult makeError (std::string_view token, std::size_t offset, std::string message)
{
    RomanNumeralParseResult result;
    result.error = Diagnostic { std::string (token), offset, std::move (message) };
    return result;
}
}

RomanNumeralParseResult parseRomanNumeral (std::string_view token,
                                           const Key& key,
                                           std::size_t offset)
{
    if (token.empty())
        return makeError (token, offset, "empty Roman numeral");

    std::size_t position = 0;
    int accidental = 0;

    while (position < token.size() && isAccidental (token[position]))
    {
        accidental += token[position] == '#' ? 1 : -1;
        ++position;
    }

    const auto numeral = longestNumeral (token, position);

    if (numeral.length == 0)
        return makeError (token, offset, "unknown Roman numeral");

    position += numeral.length;

    if (token.find ('/', position) != std::string_view::npos)
        return makeError (token, offset, "slash bass is not supported on Roman numerals");

    const auto quality = longestQuality (token, position);
    position += quality.length;

    std::vector<std::pair<int, int>> alterations;

    while (position < token.size())
    {
        if (isAccidental (token[position]) && position + 1 < token.size()
            && (token[position + 1] == '5' || token[position + 1] == '9'))
        {
            alterations.emplace_back (token[position + 1] - '0',
                                      token[position] == '#' ? 1 : -1);
            position += 2;
            continue;
        }

        return makeError (token, offset,
                          "unknown extension \"" + std::string (token.substr (position)) + "\"");
    }

    auto root = key.degree (numeral.degree);
    root.alteration += accidental;

    RomanNumeralParseResult result;
    result.chord = ChordSpec { root, resolveQuality (numeral.lowercase, quality),
                               std::move (alterations), std::nullopt, std::string (token) };
    return result;
}
}
