#include "Theory/ChordSymbolParser.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace chordtext
{
namespace
{
struct QualityToken
{
    std::string_view text;
    std::string_view canonical;
};

constexpr std::array<QualityToken, 32> qualityTokens {{
    {"9sus4", "9sus4"},
    {"maj9", "maj9"},
    {"mMaj7", "mMaj7"},
    {"minMaj7", "mMaj7"},
    {"min9", "m9"},
    {"min7", "m7"},
    {"min6", "m6"},
    {"min", "m"},
    {"m7b5", "m7b5"},
    {"m7", "m7"},
    {"m6", "m6"},
    {"m9", "m9"},
    {"maj7", "maj7"},
    {"M9", "maj9"},
    {"M7", "maj7"},
    {"add9", "add9"},
    {"dim7", "dim7"},
    {"dim", "dim"},
    {"sus2", "sus2"},
    {"sus4", "sus4"},
    {"7sus4", "7sus4"},
    {"ø7", "m7b5"},
    {"°7", "dim7"},
    {"o7", "dim7"},
    {"°", "dim"},
    {"o", "dim"},
    {"+", "aug"},
    {"6", "6"},
    {"7", "7"},
    {"5", "5"},
    {"9", "9"},
    {"m", "m"}
}};

bool isRootLetter (char character)
{
    return character >= 'A' && character <= 'G';
}

bool isAccidental (char character)
{
    return character == '#' || character == 'b';
}

struct QualityMatch
{
    std::string_view canonical;
    std::size_t length = 0;
};

QualityMatch longestQuality (std::string_view text, std::size_t position)
{
    QualityMatch best;

    for (const auto& quality : qualityTokens)
    {
        if (quality.text.size() <= best.length || quality.text.size() > text.size() - position)
            continue;

        if (text.compare (position, quality.text.size(), quality.text) == 0)
            best = { quality.canonical, quality.text.size() };
    }

    return best;
}

ChordSymbolParseResult makeError (std::string_view token, std::size_t offset, std::string message)
{
    ChordSymbolParseResult result;
    result.error = Diagnostic { std::string (token), offset, std::move (message) };
    return result;
}
}

ChordSymbolParseResult parseChordSymbol (std::string_view token, std::size_t offset)
{
    if (token.empty() || ! isRootLetter (token.front()))
        return makeError (token, offset, "unknown chord root");

    PitchClass root { token.front(), 0 };
    std::size_t position = 1;

    if (position < token.size() && isAccidental (token[position]))
    {
        root.alteration = token[position] == '#' ? 1 : -1;
        ++position;
    }

    const auto quality = longestQuality (token, position);
    position += quality.length;

    std::vector<std::pair<int, int>> alterations;

    while (position < token.size() && token[position] != '/')
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

    std::optional<PitchClass> slashBass;

    if (position < token.size())
    {
        const auto bassStart = position;
        ++position;

        if (position >= token.size() || ! isRootLetter (token[position]))
            return makeError (token, offset,
                              "invalid slash bass \"" + std::string (token.substr (bassStart)) + "\"");

        PitchClass bass { token[position], 0 };
        ++position;

        if (position < token.size() && isAccidental (token[position]))
        {
            bass.alteration = token[position] == '#' ? 1 : -1;
            ++position;
        }

        if (position != token.size())
            return makeError (token, offset,
                              "invalid slash bass \"" + std::string (token.substr (bassStart)) + "\"");

        slashBass = bass;
    }

    ChordSymbolParseResult result;
    result.chord = ChordSpec { root, std::string (quality.canonical), std::move (alterations),
                               slashBass, std::string (token) };
    return result;
}
}
