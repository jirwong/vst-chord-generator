#include "Cli/CliSession.h"

#include "Cli/CliFile.h"
#include "Cli/CliReport.h"
#include "Theory/MidiFileWriter.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace chordtext::cli
{
namespace
{
bool isAsciiSpace (char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string_view trimAscii (std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && isAsciiSpace (text[begin]))
        ++begin;

    std::size_t end = text.size();
    while (end > begin && isAsciiSpace (text[end - 1]))
        --end;

    return text.substr (begin, end - begin);
}

bool equalsIgnoreCase (std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        const auto lower = [] (char c)
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

std::string invalidValueMessage (std::string_view command)
{
    return "Error: invalid value for :" + std::string (command) + "\n";
}

std::string helpMessage (const CliSettings& settings)
{
    return
        "Commands:\n"
        "  :key <tonic>                    Set the key tonic (C, C#, Db, ...).\n"
        "  :mode <major|minor>             Set the mode.\n"
        "  :voicing <voice-led|root-position>\n"
        "  :range <C2-C5|C3-C6|C4-C7>      Set the pitch range.\n"
        "  :velocity <1-127>               Set the note velocity.\n"
        "  :bpm <4-999>                    Set the tempo.\n"
        "  :json <on|off>                  Toggle JSON report output.\n"
        "  :export <path>                  Write the last valid progression to MIDI.\n"
        "  :help                           Show this help.\n"
        "  :quit, :q                       Exit the session.\n"
        + formatSettingsLine (settings) + "\n";
}
}

CliSession::CliSession (CliSettings settings) : settings_ (std::move (settings))
{
}

const CliSettings& CliSession::settings() const
{
    return settings_;
}

const std::optional<std::string>& CliSession::lastValidText() const
{
    return lastValidText_;
}

CliSessionOutput CliSession::handleLine (std::string_view line)
{
    const auto trimmed = trimAscii (line);
    if (trimmed.empty())
        return {};

    if (trimmed.front() != ':')
    {
        CliSessionOutput output;
        const auto report = buildReport (line, settings_);

        if (report.valid)
            lastValidText_ = std::string (line);

        output.report = settings_.json ? formatJson (report) : formatHuman (report);
        return output;
    }

    const auto body = trimmed.substr (1);

    std::string_view command;
    std::string_view rest;

    const auto separator = body.find_first_of (" \t\r\n\f\v");
    if (separator == std::string_view::npos)
    {
        command = body;
        rest = {};
    }
    else
    {
        command = body.substr (0, separator);
        rest = trimAscii (body.substr (separator));
    }

    CliSessionOutput output;

    if (command == "key")
    {
        if (rest.empty())
        {
            output.feedback = invalidValueMessage (command);
        }
        else if (const auto value = parseTonic (rest))
        {
            settings_.tonic = *value;
            output.feedback = formatSettingsLine (settings_) + "\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "mode")
    {
        if (rest.empty())
        {
            output.feedback = invalidValueMessage (command);
        }
        else if (const auto value = parseMode (rest))
        {
            settings_.mode = *value;
            output.feedback = formatSettingsLine (settings_) + "\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "voicing")
    {
        if (rest.empty())
        {
            output.feedback = invalidValueMessage (command);
        }
        else if (const auto value = parseVoicing (rest))
        {
            settings_.voicing = *value;
            output.feedback = formatSettingsLine (settings_) + "\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "range")
    {
        if (rest.empty())
        {
            output.feedback = invalidValueMessage (command);
        }
        else if (const auto value = parseRangeIndex (rest))
        {
            settings_.rangeIndex = *value;
            output.feedback = formatSettingsLine (settings_) + "\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "velocity")
    {
        if (rest.empty())
        {
            output.feedback = invalidValueMessage (command);
        }
        else if (const auto value = parseVelocity (rest))
        {
            settings_.velocity = *value;
            output.feedback = formatSettingsLine (settings_) + "\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "bpm")
    {
        if (rest.empty())
        {
            output.feedback = invalidValueMessage (command);
        }
        else if (const auto value = parseBpm (rest))
        {
            settings_.bpm = *value;
            output.feedback = formatSettingsLine (settings_) + "\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "json")
    {
        if (equalsIgnoreCase (rest, "on"))
        {
            settings_.json = true;
            output.feedback = "JSON mode on\n";
        }
        else if (equalsIgnoreCase (rest, "off"))
        {
            settings_.json = false;
            output.feedback = "JSON mode off\n";
        }
        else
        {
            output.feedback = invalidValueMessage (command);
        }
    }
    else if (command == "help")
    {
        output.feedback = helpMessage (settings_);
    }
    else if (command == "quit" || command == "q")
    {
        output.quit = true;
    }
    else if (command == "export")
    {
        if (! lastValidText_.has_value())
        {
            output.feedback = "Nothing to export: no valid progression yet.\n";
            return output;
        }

        const auto report = buildReport (*lastValidText_, settings_);

        if (report.plan.events.empty())
        {
            output.feedback = "Cannot write MIDI for an empty progression.\n";
            return output;
        }

        if (rest.empty())
        {
            output.feedback = "Error: missing path for :export\n";
            return output;
        }

        const auto path = resolveMidiPath (rest);
        std::string error;

        if (chordtext::writeMidiFile (report.plan, path, settings_.midiFileSettings(), error))
        {
            output.feedback = "Wrote " + path.u8string() + "\n";
        }
        else
        {
            std::error_code ignored;
            std::filesystem::remove (path, ignored);
            output.feedback = "Error: " + error + "\n";
        }
    }
    else
    {
        output.feedback = "Unknown command: " + std::string (command) + "\n";
    }

    return output;
}
}
