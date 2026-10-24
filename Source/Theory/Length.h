#pragma once

#include "Theory/Diagnostic.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace chordtext
{
enum class LengthUnit
{
    beats,
    note,
    bars
};

enum class LengthModifier
{
    plain,
    dotted,
    triplet
};

struct LengthSpec
{
    LengthUnit unit = LengthUnit::beats;
    double quantity = 4.0;
    int denominator = 1;
    LengthModifier modifier = LengthModifier::plain;
};

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;
};

constexpr bool operator== (TimeSignature left, TimeSignature right) noexcept
{
    return left.numerator == right.numerator && left.denominator == right.denominator;
}

constexpr bool operator!= (TimeSignature left, TimeSignature right) noexcept
{
    return ! (left == right);
}

struct TimingSettings
{
    LengthSpec defaultLength;
    TimeSignature signature;
};

struct LengthParseResult
{
    std::optional<LengthSpec> value;
    std::optional<Diagnostic> error;
};

struct LengthPreset
{
    std::string_view expression;
    std::string_view displayName;
    LengthSpec length;
};

LengthParseResult parseLength (std::string_view text, std::size_t offset = 0);
std::optional<TimeSignature> parseTimeSignature (std::string_view text);
bool isValidTimeSignature (TimeSignature signature) noexcept;
std::optional<double> resolveLength (const LengthSpec& length, TimeSignature signature) noexcept;
std::string formatLength (const LengthSpec& length);
std::string formatBeatCount (double beats);
const std::array<LengthPreset, 19>& lengthPresets();
}
