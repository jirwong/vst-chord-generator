#include "Theory/Length.h"

#include <charconv>
#include <cmath>
#include <cctype>
#include <string>
#include <system_error>

namespace chordtext
{
namespace
{
bool isSupportedDenominator (int denominator) noexcept
{
    switch (denominator)
    {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32:
        case 64:
            return true;
        default:
            return false;
    }
}

bool parsePositiveFinite (std::string_view text, double& value)
{
    if (text.empty())
        return false;

    const auto conversion = std::from_chars (text.data(), text.data() + text.size(), value,
                                             std::chars_format::general);
    return conversion.ec == std::errc() && conversion.ptr == text.data() + text.size()
        && std::isfinite (value) && value > 0.0;
}

bool parsePositiveInteger (std::string_view text, int& value)
{
    if (text.empty())
        return false;

    const auto conversion = std::from_chars (text.data(), text.data() + text.size(), value);
    return conversion.ec == std::errc() && conversion.ptr == text.data() + text.size()
        && value > 0;
}

char lowerAscii (char character) noexcept
{
    return static_cast<char> (std::tolower (static_cast<unsigned char> (character)));
}

bool endsWithIgnoringCase (std::string_view text, std::string_view suffix) noexcept
{
    if (suffix.size() > text.size())
        return false;

    const auto offset = text.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index)
        if (lowerAscii (text[offset + index]) != lowerAscii (suffix[index]))
            return false;

    return true;
}

bool containsWhitespace (std::string_view text) noexcept
{
    for (const auto character : text)
        if (std::isspace (static_cast<unsigned char> (character)) != 0)
            return true;

    return false;
}

LengthParseResult invalidLength (std::string_view text, std::size_t offset)
{
    return {{}, Diagnostic {std::string (text), offset,
                            "invalid length \"" + std::string (text) + "\""}};
}

std::string formatNumber (double value)
{
    char buffer[64];
    const auto result = std::to_chars (buffer, buffer + sizeof (buffer), value,
                                       std::chars_format::general);
    if (result.ec != std::errc())
        return {};

    return std::string (buffer, result.ptr);
}

bool approximatelyEqual (double left, double right) noexcept
{
    return std::abs (left - right) < 1.0e-12;
}

std::string pluralisedBeatLabel (std::string value, double beats)
{
    return value + (approximatelyEqual (beats, 1.0) ? " beat" : " beats");
}
}

LengthParseResult parseLength (std::string_view text, std::size_t offset)
{
    if (text.empty() || containsWhitespace (text))
        return invalidLength (text, offset);

    const auto slash = text.find ('/');
    if (slash != std::string_view::npos)
    {
        if (text.find ('/', slash + 1) != std::string_view::npos)
            return invalidLength (text, offset);

        auto denominatorText = text.substr (slash + 1);
        auto modifier = LengthModifier::plain;

        if (! denominatorText.empty())
        {
            const auto suffix = lowerAscii (denominatorText.back());
            if (suffix == 'd' || suffix == 't')
            {
                modifier = suffix == 'd' ? LengthModifier::dotted : LengthModifier::triplet;
                denominatorText.remove_suffix (1);
            }
        }

        int numerator = 0;
        int denominator = 0;
        if (! parsePositiveInteger (text.substr (0, slash), numerator)
            || ! parsePositiveInteger (denominatorText, denominator)
            || ! isSupportedDenominator (denominator))
            return invalidLength (text, offset);

        return {LengthSpec {LengthUnit::note, static_cast<double> (numerator), denominator, modifier}, {}};
    }

    double quantity = 0.0;
    if (parsePositiveFinite (text, quantity))
        return {LengthSpec {LengthUnit::beats, quantity, 1, LengthModifier::plain}, {}};

    constexpr std::array<std::pair<std::string_view, LengthUnit>, 4> units {{
        {"beats", LengthUnit::beats}, {"beat", LengthUnit::beats},
        {"bars", LengthUnit::bars}, {"bar", LengthUnit::bars}
    }};

    for (const auto& unit : units)
    {
        if (! endsWithIgnoringCase (text, unit.first))
            continue;

        const auto quantityText = text.substr (0, text.size() - unit.first.size());
        if (! parsePositiveFinite (quantityText, quantity))
            return invalidLength (text, offset);

        return {LengthSpec {unit.second, quantity, 1, LengthModifier::plain}, {}};
    }

    return invalidLength (text, offset);
}

std::optional<TimeSignature> parseTimeSignature (std::string_view text)
{
    if (text.empty() || containsWhitespace (text))
        return std::nullopt;

    const auto slash = text.find ('/');
    if (slash == std::string_view::npos || text.find ('/', slash + 1) != std::string_view::npos)
        return std::nullopt;

    TimeSignature result;
    if (! parsePositiveInteger (text.substr (0, slash), result.numerator)
        || ! parsePositiveInteger (text.substr (slash + 1), result.denominator)
        || ! isValidTimeSignature (result))
        return std::nullopt;

    return result;
}

bool isValidTimeSignature (TimeSignature signature) noexcept
{
    return signature.numerator >= 1 && signature.numerator <= 255
        && isSupportedDenominator (signature.denominator);
}

std::optional<double> resolveLength (const LengthSpec& length, TimeSignature signature) noexcept
{
    if (! isValidTimeSignature (signature) || ! std::isfinite (length.quantity)
        || length.quantity <= 0.0)
        return std::nullopt;

    double beats = 0.0;
    switch (length.unit)
    {
        case LengthUnit::beats:
            if (length.denominator != 1 || length.modifier != LengthModifier::plain)
                return std::nullopt;
            beats = length.quantity;
            break;

        case LengthUnit::note:
            if (std::floor (length.quantity) != length.quantity
                || ! isSupportedDenominator (length.denominator))
                return std::nullopt;

            beats = 4.0 * length.quantity / static_cast<double> (length.denominator);
            switch (length.modifier)
            {
                case LengthModifier::plain: break;
                case LengthModifier::dotted: beats *= 1.5; break;
                case LengthModifier::triplet: beats *= 2.0 / 3.0; break;
            }
            break;

        case LengthUnit::bars:
            if (length.denominator != 1 || length.modifier != LengthModifier::plain)
                return std::nullopt;
            beats = length.quantity * static_cast<double> (signature.numerator) * 4.0
                  / static_cast<double> (signature.denominator);
            break;
    }

    if (! std::isfinite (beats) || beats <= 0.0)
        return std::nullopt;

    return beats;
}

std::string formatLength (const LengthSpec& length)
{
    const auto quantity = formatNumber (length.quantity);
    switch (length.unit)
    {
        case LengthUnit::beats:
            return quantity + (approximatelyEqual (length.quantity, 1.0) ? "beat" : "beats");

        case LengthUnit::note:
        {
            auto result = quantity + "/" + std::to_string (length.denominator);
            if (length.modifier == LengthModifier::dotted)
                result += 'D';
            else if (length.modifier == LengthModifier::triplet)
                result += 'T';
            return result;
        }

        case LengthUnit::bars:
            return quantity + (approximatelyEqual (length.quantity, 1.0) ? "bar" : "bars");
    }

    return {};
}

std::string formatBeatCount (double beats)
{
    struct Fraction
    {
        double value;
        std::string_view text;
    };
    constexpr std::array<Fraction, 6> fractions {{
        {1.0 / 6.0, "1/6"}, {1.0 / 4.0, "1/4"}, {1.0 / 3.0, "1/3"},
        {3.0 / 8.0, "3/8"}, {1.0 / 2.0, "1/2"}, {3.0 / 4.0, "3/4"}
    }};

    for (const auto& fraction : fractions)
        if (approximatelyEqual (beats, fraction.value))
            return pluralisedBeatLabel (std::string (fraction.text), beats);

    return pluralisedBeatLabel (formatNumber (beats), beats);
}

const std::array<LengthPreset, 19>& lengthPresets()
{
    static const std::array<LengthPreset, 19> presets {{
        {"1/2", "1/2", {LengthUnit::note, 1.0, 2, LengthModifier::plain}},
        {"1/2D", "1/2D", {LengthUnit::note, 1.0, 2, LengthModifier::dotted}},
        {"1/2T", "1/2T", {LengthUnit::note, 1.0, 2, LengthModifier::triplet}},
        {"1/4", "1/4", {LengthUnit::note, 1.0, 4, LengthModifier::plain}},
        {"1/4D", "1/4D", {LengthUnit::note, 1.0, 4, LengthModifier::dotted}},
        {"1/4T", "1/4T", {LengthUnit::note, 1.0, 4, LengthModifier::triplet}},
        {"1/8", "1/8", {LengthUnit::note, 1.0, 8, LengthModifier::plain}},
        {"1/8D", "1/8D", {LengthUnit::note, 1.0, 8, LengthModifier::dotted}},
        {"1/8T", "1/8T", {LengthUnit::note, 1.0, 8, LengthModifier::triplet}},
        {"1/16", "1/16", {LengthUnit::note, 1.0, 16, LengthModifier::plain}},
        {"1/16D", "1/16D", {LengthUnit::note, 1.0, 16, LengthModifier::dotted}},
        {"1/16T", "1/16T", {LengthUnit::note, 1.0, 16, LengthModifier::triplet}},
        {"3/16", "3/16", {LengthUnit::note, 3.0, 16, LengthModifier::plain}},
        {"1beat", "1 beat", {LengthUnit::beats, 1.0, 1, LengthModifier::plain}},
        {"2beats", "2 beats", {LengthUnit::beats, 2.0, 1, LengthModifier::plain}},
        {"4beats", "4 beats", {LengthUnit::beats, 4.0, 1, LengthModifier::plain}},
        {"1bar", "1 bar", {LengthUnit::bars, 1.0, 1, LengthModifier::plain}},
        {"2bars", "2 bars", {LengthUnit::bars, 2.0, 1, LengthModifier::plain}},
        {"4bars", "4 bars", {LengthUnit::bars, 4.0, 1, LengthModifier::plain}}
    }};
    return presets;
}
}
