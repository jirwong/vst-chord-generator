#include "Theory/Progression.h"

#include "Theory/ChordSymbolParser.h"
#include "Theory/CustomChordParser.h"
#include "Theory/RomanNumeralParser.h"
#include "Theory/NumericDegreeParser.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iterator>
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
        const auto startsCustomChord = text[position] == '[';
        auto bracketDepth = 0;

        while (position < text.size())
        {
            const auto character = text[position];
            if (startsCustomChord && bracketDepth > 0)
            {
                if (character == '[')
                    ++bracketDepth;
                else if (character == ']')
                    --bracketDepth;
                ++position;
                continue;
            }

            if (isSeparator (character))
                break;

            if (startsCustomChord && character == '[')
                ++bracketDepth;
            ++position;
        }

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
        int numericDegree = 0;

        const auto digit = [] (char c) { return c >= '0' && c <= '9'; };
        const auto numeric = digit (chordText.front())
            || ((chordText.front() == 'b' || chordText.front() == '#')
                && chordText.size() > 1 && digit (chordText[1]));
        if (! chordText.empty() && chordText.front() == '[')
        {
            auto parsed = parseCustomChord (chordText, key, start);
            if (! parsed.diagnostics.empty())
            {
                result.diagnostics.insert (result.diagnostics.end(),
                                           std::make_move_iterator (parsed.diagnostics.begin()),
                                           std::make_move_iterator (parsed.diagnostics.end()));
                continue;
            }
            spec = std::move (parsed.spec);
        }
        else if (numeric)
        {
            auto parsed = parseNumericDegree (chordText, key, start);
            spec = std::move (parsed.chord);
            error = std::move (parsed.error);
            numericDegree = parsed.degree;
        }
        else if (isRomanToken (chordText))
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
                                                       std::string (chordText), start, numericDegree });
    }

    if (result.diagnostics.empty())
        result.progression = std::move (progression);

    return result;
}
}
