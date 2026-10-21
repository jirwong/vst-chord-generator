#include "Theory/Progression.h"

#include "Theory/ChordSymbolParser.h"
#include "Theory/RomanNumeralParser.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace chordtext
{
namespace
{
bool isSeparator (char character)
{
    return character == ',' || character == '|' || character == '-'
        || std::isspace (static_cast<unsigned char> (character)) != 0;
}

bool isRomanToken (std::string_view token)
{
    if (token.empty())
        return false;

    const auto first = token.front();

    if (first == 'I' || first == 'i' || first == 'V' || first == 'v')
        return true;

    if ((first == 'b' || first == '#') && token.size() > 1)
    {
        const auto second = token[1];
        return second == 'I' || second == 'i' || second == 'V' || second == 'v';
    }

    return false;
}

bool isAbsoluteToken (std::string_view token)
{
    return ! token.empty() && token.front() >= 'A' && token.front() <= 'G';
}

bool parseDuration (std::string_view text, double& duration)
{
    if (text.empty())
        return false;

    double value = 0.0;
    const auto conversion = std::from_chars (text.data(), text.data() + text.size(), value,
                                             std::chars_format::general);

    if (conversion.ec != std::errc() || conversion.ptr != text.data() + text.size())
        return false;

    if (! std::isfinite (value) || value <= 0.0)
        return false;

    duration = value;
    return true;
}

Diagnostic makeDiagnostic (std::string_view token, std::size_t offset, std::string message)
{
    return Diagnostic { std::string (token), offset, std::move (message) };
}
}

double Progression::totalBeats() const
{
    double total = 0.0;

    for (const auto& step : steps)
        total += step.durationBeats;

    return total;
}

ProgressionParseResult parseProgression (std::string_view text, const Key& key)
{
    ProgressionParseResult result;
    Progression progression;

    std::size_t position = 0;

    while (position < text.size())
    {
        if (isSeparator (text[position]))
        {
            ++position;
            continue;
        }

        const auto start = position;

        while (position < text.size() && ! isSeparator (text[position]))
            ++position;

        const auto raw = text.substr (start, position - start);
        const auto colon = raw.find (':');

        auto chordText = raw;
        double duration = 4.0;
        bool validDuration = true;

        if (colon != std::string_view::npos)
        {
            chordText = raw.substr (0, colon);
            validDuration = parseDuration (raw.substr (colon + 1), duration);
        }

        if (chordText.empty())
        {
            result.diagnostics.push_back (makeDiagnostic (raw, start, "missing chord symbol"));
            continue;
        }

        if (! validDuration)
        {
            result.diagnostics.push_back (makeDiagnostic (
                raw, start, "invalid duration \"" + std::string (raw.substr (colon + 1)) + "\""));
            continue;
        }

        std::optional<ChordSpec> spec;
        std::optional<Diagnostic> error;

        if (isRomanToken (chordText))
        {
            auto parsed = parseRomanNumeral (chordText, key, start);
            spec = std::move (parsed.chord);
            error = std::move (parsed.error);
        }
        else if (isAbsoluteToken (chordText))
        {
            auto parsed = parseChordSymbol (chordText, start);
            spec = std::move (parsed.chord);
            error = std::move (parsed.error);
        }
        else
        {
            error = makeDiagnostic (chordText, start, "unknown chord root");
        }

        if (! spec.has_value())
        {
            result.diagnostics.push_back (error.has_value()
                                              ? std::move (*error)
                                              : makeDiagnostic (chordText, start, "invalid chord"));
            continue;
        }

        progression.steps.push_back (ProgressionStep { Chord (std::move (*spec)), duration,
                                                       std::string (chordText), start });
    }

    if (result.diagnostics.empty())
        result.progression = std::move (progression);

    return result;
}
}
