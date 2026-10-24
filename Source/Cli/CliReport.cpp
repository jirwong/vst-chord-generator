#include "Cli/CliReport.h"

#include "Theory/Chord.h"
#include "Theory/ChordResolution.h"
#include "Theory/CustomChordParser.h"
#include "Theory/Progression.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chordtext::cli
{
namespace
{
std::string formatNumber (double value)
{
    char buffer[64];
    const auto result = std::to_chars (buffer, buffer + sizeof (buffer), value,
                                        std::chars_format::general);
    return std::string (buffer, result.ptr);
}

std::string collapseWhitespace (std::string_view text)
{
    std::string result;
    bool pendingSpace = false;

    for (const auto byte : text)
    {
        const auto c = static_cast<unsigned char> (byte);
        const bool isSpace = c == ' ' || c == '\t' || c == '\n' || c == '\r'
                          || c == '\f' || c == '\v';

        if (isSpace)
        {
            pendingSpace = true;
            continue;
        }

        if (pendingSpace && ! result.empty())
            result += ' ';

        pendingSpace = false;
        result += static_cast<char> (c);
    }

    return result;
}

void appendJsonString (std::string& out, std::string_view value)
{
    out += '"';

    for (const auto byte : value)
    {
        const auto c = static_cast<unsigned char> (byte);

        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    constexpr char hex[] = "0123456789ABCDEF";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                }
                else
                {
                    out += static_cast<char> (c);
                }
                break;
        }
    }

    out += '"';
}

void appendJsonStringList (std::string& out, const std::vector<std::string>& values)
{
    out += '[';

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out += ", ";

        appendJsonString (out, values[i]);
    }

    out += ']';
}

void appendJsonIntList (std::string& out, const std::vector<int>& values)
{
    out += '[';

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out += ", ";

        out += std::to_string (values[i]);
    }

    out += ']';
}

std::string joinStrings (const std::vector<std::string>& values)
{
    std::string result;

    for (const auto& value : values)
    {
        if (! result.empty())
            result += ' ';

        result += value;
    }

    return result;
}

std::string joinInts (const std::vector<int>& values)
{
    std::string result;

    for (const auto value : values)
    {
        if (! result.empty())
            result += ' ';

        result += std::to_string (value);
    }

    return result;
}

void rightTrim (std::string& value)
{
    while (! value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.pop_back();
}
}

CliReport buildReport (std::string_view text, const CliSettings& settings)
{
    CliReport report;
    report.text = std::string (text);
    report.settings = settings;

    const auto key = settings.key();
    const auto parsed = parseProgression (report.text, key);
    report.diagnostics = parsed.diagnostics;

    if (! parsed.progression.has_value())
        return report;

    const auto& progression = *parsed.progression;
    const auto resolution = resolveProgression (progression, settings.voicingSettings(),
                                                settings.bassOctaves);
    if (! resolution.chords.has_value())
    {
        report.diagnostics.insert (report.diagnostics.end(), resolution.diagnostics.begin(),
                                   resolution.diagnostics.end());
        return report;
    }

    report.valid = true;
    std::vector<std::vector<int>> voicings;
    voicings.reserve (resolution.chords->size());
    for (const auto& chord : *resolution.chords)
        voicings.push_back (chord.pitches);
    report.plan = buildRenderPlan (progression, voicings, settings.velocity);

    report.chords.reserve (progression.steps.size());

    double startBeat = 0.0;

    for (std::size_t index = 0; index < progression.steps.size(); ++index)
    {
        const auto& step = progression.steps[index];

        CliChordRow row;
        row.source = step.sourceText;
        row.symbol = step.chord.symbol();
        row.degree = degreeLabel (step.chord, key);

        for (const auto& tone : step.chord.tones())
            row.spelledTones.push_back (tone.name());

        row.voicing = resolution.chords->at (index).pitches;
        row.bassMidi = resolution.chords->at (index).bassMidi;

        const auto& rootSpelling = step.chord.spec().customNotes.has_value()
            ? step.chord.spec().customNotes->front().pitchClass : step.chord.spec().root;
        if (row.bassMidi.has_value())
            row.bassNote = voicedNoteName (rootSpelling, *row.bassMidi);

        for (const auto pitch : row.voicing)
        {
            if (step.chord.spec().customNotes.has_value())
            {
                const auto& resolvedChord = resolution.chords->at (index);
                const auto resolvedNote = std::lower_bound (resolvedChord.chordPitches.begin(),
                                                            resolvedChord.chordPitches.end(), pitch);
                if (resolvedNote != resolvedChord.chordPitches.end() && *resolvedNote == pitch)
                {
                    const auto noteIndex = static_cast<std::size_t> (
                        resolvedNote - resolvedChord.chordPitches.begin());
                    row.notes.push_back (voicedNoteName (
                        resolvedChord.chordSpellings[noteIndex], pitch));
                }
                else if (row.bassMidi == pitch && row.bassNote.has_value())
                {
                    row.notes.push_back (*row.bassNote);
                }
                else
                {
                    row.notes.push_back (noteNameForPitch (step.chord, pitch));
                }
            }
            else
            {
                row.notes.push_back (noteNameForPitch (step.chord, pitch));
            }
        }

        row.startBeat = startBeat;
        row.durationBeats = step.durationBeats;
        startBeat += step.durationBeats;

        report.chords.push_back (std::move (row));
    }

    return report;
}

std::string formatHuman (const CliReport& report)
{
    std::vector<std::string> lines;

    const auto collapsed = collapseWhitespace (report.text);
    lines.push_back ("Progression: " + (collapsed.empty() ? "(empty)" : collapsed));

    lines.push_back (formatSettingsLine (report.settings));

    lines.push_back ("");

    if (! report.diagnostics.empty())
    {
        const auto count = report.diagnostics.size();
        lines.push_back (count == 1 ? "1 error:" : std::to_string (count) + " errors:");

        for (const auto& diagnostic : report.diagnostics)
        {
            std::string line = "Error at character " + std::to_string (diagnostic.offset)
                             + ": " + diagnostic.message;

            if (! diagnostic.token.empty())
                line += " (" + diagnostic.token + ")";

            lines.push_back (std::move (line));
        }
    }
    else if (report.chords.empty())
    {
        lines.push_back ("Empty progression (no chords).");
    }
    else
    {
        std::vector<std::string> headers { "Chord", "Degree", "Notes", "Beats", "MIDI" };
        if (report.settings.bassOctaves > 0)
            headers.push_back ("Bass");

        std::vector<std::vector<std::string>> cells;
        cells.reserve (report.chords.size());

        for (const auto& row : report.chords)
        {
            auto cellsForRow = std::vector<std::string> {
                row.symbol,
                row.degree,
                joinStrings (row.notes),
                formatNumber (row.durationBeats),
                joinInts (row.voicing)
            };
            if (report.settings.bassOctaves > 0)
                cellsForRow.push_back (row.bassNote.value_or ("-"));
            cells.push_back (std::move (cellsForRow));
        }

        std::vector<std::size_t> widths (headers.size(), 0);
        for (std::size_t col = 0; col < headers.size(); ++col)
            widths[col] = headers[col].size();

        for (const auto& row : cells)
            for (std::size_t col = 0; col < row.size(); ++col)
                widths[col] = std::max (widths[col], row[col].size());

        const auto emitRow = [] (const std::vector<std::string>& values,
                                 const std::vector<std::size_t>& widths)
        {
            std::string line;

            for (std::size_t col = 0; col < values.size(); ++col)
            {
                if (col != 0)
                    line += "  ";

                line += values[col];
                line.append (widths[col] - values[col].size(), ' ');
            }

            rightTrim (line);
            return line;
        };

        lines.push_back (emitRow (headers, widths));
        for (const auto& row : cells)
            lines.push_back (emitRow (row, widths));
    }

    if (report.midiFile.has_value())
        lines.push_back ("Wrote " + *report.midiFile);

    std::string result;

    for (const auto& line : lines)
    {
        result += line;
        result += '\n';
    }

    return result;
}

std::string formatJson (const CliReport& report)
{
    std::string out;

    out += "{\n";

    out += "  \"text\": ";
    appendJsonString (out, report.text);
    out += ",\n";

    out += "  \"settings\": {\n";
    out += "    \"key\": ";
    appendJsonString (out, report.settings.keyName());
    out += ",\n";
    out += "    \"mode\": ";
    appendJsonString (out, report.settings.modeName());
    out += ",\n";
    out += "    \"voicing\": ";
    appendJsonString (out, report.settings.voicingName());
    out += ",\n";
    out += "    \"range\": ";
    appendJsonString (out, report.settings.rangeName());
    out += ",\n";
    out += "    \"velocity\": " + std::to_string (report.settings.velocity) + ",\n";
    out += "    \"bpm\": " + formatNumber (report.settings.bpm) + ",\n";
    out += "    \"bassOctaves\": " + std::to_string (report.settings.bassOctaves) + "\n";
    out += "  },\n";

    out += "  \"valid\": ";
    out += report.valid ? "true" : "false";
    out += ",\n";

    out += "  \"diagnostics\": ";
    if (report.diagnostics.empty())
    {
        out += "[]";
    }
    else
    {
        out += "[\n";

        for (std::size_t i = 0; i < report.diagnostics.size(); ++i)
        {
            const auto& diagnostic = report.diagnostics[i];

            out += "    {\n";
            out += "      \"offset\": " + std::to_string (diagnostic.offset) + ",\n";
            out += "      \"token\": ";
            appendJsonString (out, diagnostic.token);
            out += ",\n";
            out += "      \"message\": ";
            appendJsonString (out, diagnostic.message);
            out += "\n";
            out += "    }";

            if (i + 1 != report.diagnostics.size())
                out += ",";

            out += "\n";
        }

        out += "  ]";
    }
    out += ",\n";

    out += "  \"chords\": ";
    if (report.chords.empty())
    {
        out += "[]";
    }
    else
    {
        out += "[\n";

        for (std::size_t i = 0; i < report.chords.size(); ++i)
        {
            const auto& chord = report.chords[i];

            out += "    {\n";
            out += "      \"source\": ";
            appendJsonString (out, chord.source);
            out += ",\n";
            out += "      \"symbol\": ";
            appendJsonString (out, chord.symbol);
            out += ",\n";
            out += "      \"degree\": ";
            appendJsonString (out, chord.degree);
            out += ",\n";
            out += "      \"spelledTones\": ";
            appendJsonStringList (out, chord.spelledTones);
            out += ",\n";
            out += "      \"notes\": ";
            appendJsonStringList (out, chord.notes);
            out += ",\n";
            out += "      \"voicing\": ";
            appendJsonIntList (out, chord.voicing);
            out += ",\n";
            out += "      \"bassMidi\": ";
            if (chord.bassMidi.has_value())
                out += std::to_string (*chord.bassMidi);
            else
                out += "null";
            out += ",\n";
            out += "      \"bassNote\": ";
            if (chord.bassNote.has_value())
                appendJsonString (out, *chord.bassNote);
            else
                out += "null";
            out += ",\n";
            out += "      \"startBeat\": " + formatNumber (chord.startBeat) + ",\n";
            out += "      \"durationBeats\": " + formatNumber (chord.durationBeats) + "\n";
            out += "    }";

            if (i + 1 != report.chords.size())
                out += ",";

            out += "\n";
        }

        out += "  ]";
    }
    out += ",\n";

    out += "  \"midiFile\": ";
    if (report.midiFile.has_value())
        appendJsonString (out, *report.midiFile);
    else
        out += "null";
    out += ",\n";

    out += "  \"error\": ";
    if (report.error.has_value())
        appendJsonString (out, *report.error);
    else
        out += "null";
    out += "\n";

    out += "}\n";

    return out;
}
}
