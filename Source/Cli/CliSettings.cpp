#include "Cli/CliSettings.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace chordtext::cli
{
namespace
{
constexpr std::array<int, 3> rangeLows { 36, 48, 60 };
constexpr std::array<int, 3> rangeHighs { 72, 84, 96 };
constexpr std::array<const char*, 3> rangeLabels { "C2-C5", "C3-C6", "C4-C7" };

int clampRangeIndex (int index)
{
    if (index < 0)
        return 0;
    if (index > 2)
        return 2;
    return index;
}

bool equalsIgnoreCase (std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        auto lower = [] (char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char> (c - 'A' + 'a');
            return c;
        };

        if (lower (lhs[i]) != lower (rhs[i]))
            return false;
    }

    return true;
}
}

Key CliSettings::key() const
{
    return Key { tonic, mode };
}

VoicingSettings CliSettings::voicingSettings() const
{
    const auto index = clampRangeIndex (rangeIndex);
    return VoicingSettings { voicing, rangeLows[static_cast<std::size_t> (index)],
                             rangeHighs[static_cast<std::size_t> (index)] };
}

MidiFileSettings CliSettings::midiFileSettings() const
{
    return MidiFileSettings { bpm, true, "Chord Text", 960 };
}

std::string CliSettings::keyName() const
{
    return tonic.name();
}

std::string CliSettings::modeName() const
{
    return mode == Mode::major ? "major" : "minor";
}

std::string CliSettings::voicingName() const
{
    return voicing == VoicingMode::voiceLed ? "voice-led" : "root-position";
}

std::string CliSettings::rangeName() const
{
    return rangeLabels[static_cast<std::size_t> (clampRangeIndex (rangeIndex))];
}

std::string formatSettingsLine (const CliSettings& settings)
{
    return "Key: " + settings.keyName() + " " + settings.modeName()
         + " · Voicing: " + settings.voicingName()
         + " · Range: " + settings.rangeName()
         + " · Velocity: " + std::to_string (settings.velocity);
}

std::optional<PitchClass> parseTonic (std::string_view text)
{
    constexpr std::array<std::string_view, 17> names {
        "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B"
    };

    for (const auto name : names)
    {
        if (! equalsIgnoreCase (name, text))
            continue;

        int alteration = 0;
        if (name.size() >= 2)
        {
            if (name[1] == '#')
                alteration = 1;
            else if (name[1] == 'b')
                alteration = -1;
        }

        return PitchClass { name[0], alteration };
    }

    return std::nullopt;
}

std::optional<Mode> parseMode (std::string_view text)
{
    if (equalsIgnoreCase (text, "major"))
        return Mode::major;
    if (equalsIgnoreCase (text, "minor"))
        return Mode::minor;
    return std::nullopt;
}

std::optional<VoicingMode> parseVoicing (std::string_view text)
{
    if (equalsIgnoreCase (text, "voice-led"))
        return VoicingMode::voiceLed;
    if (equalsIgnoreCase (text, "root-position") || equalsIgnoreCase (text, "root"))
        return VoicingMode::rootPosition;
    return std::nullopt;
}

std::optional<int> parseRangeIndex (std::string_view text)
{
    if (equalsIgnoreCase (text, "C2-C5"))
        return 0;
    if (equalsIgnoreCase (text, "C3-C6"))
        return 1;
    if (equalsIgnoreCase (text, "C4-C7"))
        return 2;
    return std::nullopt;
}

std::optional<int> parseVelocity (std::string_view text)
{
    int value = 0;
    const auto begin = text.data();
    const auto end = begin + text.size();
    const auto result = std::from_chars (begin, end, value);

    if (result.ec != std::errc() || result.ptr != end)
        return std::nullopt;
    if (value < 1 || value > 127)
        return std::nullopt;
    return value;
}

std::optional<double> parseBpm (std::string_view text)
{
    double value = 0.0;
    const auto begin = text.data();
    const auto end = begin + text.size();
    const auto result = std::from_chars (begin, end, value, std::chars_format::general);

    if (result.ec != std::errc() || result.ptr != end)
        return std::nullopt;
    if (! std::isfinite (value))
        return std::nullopt;
    if (value < 4.0 || value > 999.0)
        return std::nullopt;
    return value;
}
}
