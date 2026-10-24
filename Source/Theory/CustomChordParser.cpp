#include "Theory/CustomChordParser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace chordtext
{
namespace
{
struct EntryParseResult
{
    std::optional<CustomNoteEntry> entry;
    std::optional<Diagnostic> error;
};

int positiveModulo (int value, int modulus)
{
    return (value % modulus + modulus) % modulus;
}

std::string_view trim (std::string_view text)
{
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front())) != 0)
        text.remove_prefix (1);
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back())) != 0)
        text.remove_suffix (1);
    return text;
}

Diagnostic makeError (std::string_view token, std::size_t offset, std::string message)
{
    return {std::string (token), offset, std::move (message)};
}

bool parseSignedInteger (std::string_view text, int& value)
{
    if (text.empty())
        return false;
    const auto parsed = std::from_chars (text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
}

bool parseRelativeOffset (std::string_view suffix, int& value)
{
    if (suffix == "@-1")
    {
        value = -1;
        return true;
    }
    if (suffix == "@+1")
    {
        value = 1;
        return true;
    }
    return false;
}

EntryParseResult parseEntry (std::string_view original, const Key& key, std::size_t offset)
{
    const auto text = trim (original);
    const auto leadingWhitespace = static_cast<std::size_t> (text.data() - original.data());
    const auto entryOffset = offset + leadingWhitespace;
    const auto failure = [&] (std::string message)
    {
        return EntryParseResult {std::nullopt, makeError (text, entryOffset, std::move (message))};
    };

    if (text.empty())
        return failure ("empty custom chord entry");
    for (const auto character : text)
        if (std::isspace (static_cast<unsigned char> (character)) != 0)
            return failure ("spaces are allowed only around custom chord entries");

    CustomNoteEntry entry;
    entry.sourceText = std::string (text);
    entry.offset = entryOffset;

    const auto first = text.front();
    if (first >= 'A' && first <= 'G')
    {
        entry.pitchClass = {first, 0};
        std::size_t position = 1;
        if (position < text.size() && (text[position] == '#' || text[position] == 'b'))
        {
            entry.pitchClass.alteration = text[position] == '#' ? 1 : -1;
            ++position;
        }

        if (position == text.size())
            return {entry, std::nullopt};

        if (text[position] == '@')
        {
            if (! parseRelativeOffset (text.substr (position), entry.octaveOffset))
                return failure ("named-note relative octave must be @-1 or @+1");
            return {entry, std::nullopt};
        }

        int octave = 0;
        if (! parseSignedInteger (text.substr (position), octave) || octave < -1 || octave > 9)
            return failure ("exact note octave must be from -1 to 9");

        const auto midi = 12 * (octave + 1) + entry.pitchClass.semitone();
        if (midi < 0 || midi > 127)
            return failure ("exact note is outside the MIDI pitch range C-1 to G9");
        entry.exactMidi = midi;
        return {entry, std::nullopt};
    }

    std::size_t position = 0;
    auto accidental = 0;
    if (text[position] == 'b' || text[position] == '#')
    {
        accidental = text[position] == 'b' ? -1 : 1;
        ++position;
    }

    const auto numberStart = position;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9')
        ++position;
    if (position == numberStart)
        return failure ("entry must be a scale degree or an uppercase note name A to G");

    int degree = 0;
    if (! parseSignedInteger (text.substr (numberStart, position - numberStart), degree)
        || degree < 1 || degree > 14)
        return failure ("custom scale degree must be from 1 to 14");

    const auto isOctaveUpDegree = degree >= 8;
    const auto scaleDegree = isOctaveUpDegree ? degree - 7 : degree;
    entry.pitchClass = key.degree (scaleDegree);
    entry.pitchClass.alteration += accidental;
    entry.octaveOffset = isOctaveUpDegree ? 1 : 0;

    if (position < text.size())
    {
        if (isOctaveUpDegree)
            return failure ("degrees 8 to 14 already mean one octave up and cannot use an octave suffix");
        if (! parseRelativeOffset (text.substr (position), entry.octaveOffset))
            return failure ("degree relative octave must be @-1 or @+1");
    }

    return {entry, std::nullopt};
}

bool samePitchClass (const PitchClass& left, const PitchClass& right)
{
    return positiveModulo (left.semitone(), 12) == positiveModulo (right.semitone(), 12);
}
}

CustomChordParseResult parseCustomChord (std::string_view text, const Key& key,
                                         std::size_t offset)
{
    CustomChordParseResult result;
    if (text.size() < 2 || text.front() != '[' || text.back() != ']')
    {
        result.diagnostics.push_back (makeError (text, offset, "custom chord must use [note, note] brackets"));
        return result;
    }

    const auto body = text.substr (1, text.size() - 2);
    if (body.find_first_of ("[]") != std::string_view::npos)
    {
        result.diagnostics.push_back (makeError (text, offset, "nested or unmatched custom chord bracket"));
        return result;
    }

    std::vector<CustomNoteEntry> entries;
    std::size_t position = 0;
    while (position <= body.size())
    {
        const auto comma = body.find (',', position);
        const auto end = comma == std::string_view::npos ? body.size() : comma;
        const auto entryText = body.substr (position, end - position);
        auto parsed = parseEntry (entryText, key, offset + 1 + position);
        if (parsed.error)
        {
            result.diagnostics.push_back (std::move (*parsed.error));
            return result;
        }
        entries.push_back (std::move (*parsed.entry));
        if (entries.size() > 128)
        {
            result.diagnostics.push_back (makeError (text, offset, "custom chord cannot contain more than 128 entries"));
            return result;
        }
        if (comma == std::string_view::npos)
            break;
        position = comma + 1;
    }

    if (entries.empty())
    {
        result.diagnostics.push_back (makeError (text, offset, "custom chord needs at least one note"));
        return result;
    }

    ChordSpec spec;
    spec.root = entries.front().pitchClass;
    spec.quality = "custom";
    spec.sourceText = std::string (text);
    spec.customNotes = std::move (entries);
    result.spec = std::move (spec);
    return result;
}

std::string voicedNoteName (const PitchClass& spelling, int midi)
{
    const auto octave = (midi - spelling.semitone()) / 12 - 1;
    return spelling.name() + std::to_string (octave);
}
}
