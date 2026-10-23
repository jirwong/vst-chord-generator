#include "Cli/CliOptions.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chordtext::cli
{
CliParseResult parseOptions (const std::vector<std::string>& args)
{
    for (const auto& arg : args)
    {
        if (arg == "--")
            break;
        if (arg == "--help")
            return CliParseResult { CliParseStatus::help, {}, {} };
    }

    CliOptions options;
    std::optional<std::string> positional;
    bool positionalOnly = false;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (! positionalOnly && arg == "--")
        {
            positionalOnly = true;
            continue;
        }

        if (! positionalOnly && ! arg.empty() && arg[0] == '-')
        {
            auto name = arg;
            std::optional<std::string> attachedValue;

            const auto eq = arg.find ('=');
            if (eq != std::string::npos)
            {
                name = arg.substr (0, eq);
                attachedValue = arg.substr (eq + 1);
            }

            const auto requireValue = [&] (std::string& out) -> bool
            {
                if (attachedValue.has_value())
                {
                    out = *attachedValue;
                    return true;
                }
                if (i + 1 < args.size())
                {
                    out = args[++i];
                    return true;
                }
                return false;
            };

            const auto invalid = [&] (std::string_view option, const std::string& value)
            {
                return CliParseResult { CliParseStatus::usageError, {},
                                        "Invalid value for " + std::string (option)
                                            + ": " + value };
            };

            if (name == "--help")
            {
                if (attachedValue.has_value())
                    return invalid ("--help", *attachedValue);
                return CliParseResult { CliParseStatus::help, {}, {} };
            }
            else if (name == "--json")
            {
                if (attachedValue.has_value())
                    return invalid ("--json", *attachedValue);
                options.settings.json = true;
            }
            else if (name == "--key")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --key" };
                const auto tonic = parseTonic (value);
                if (! tonic.has_value())
                    return invalid ("--key", value);
                options.settings.tonic = *tonic;
            }
            else if (name == "--mode")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --mode" };
                const auto mode = parseMode (value);
                if (! mode.has_value())
                    return invalid ("--mode", value);
                options.settings.mode = *mode;
            }
            else if (name == "--voicing")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --voicing" };
                const auto voicing = parseVoicing (value);
                if (! voicing.has_value())
                    return invalid ("--voicing", value);
                options.settings.voicing = *voicing;
            }
            else if (name == "--range")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --range" };
                const auto rangeIndex = parseRangeIndex (value);
                if (! rangeIndex.has_value())
                    return invalid ("--range", value);
                options.settings.rangeIndex = *rangeIndex;
            }
            else if (name == "--velocity")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --velocity" };
                const auto velocity = parseVelocity (value);
                if (! velocity.has_value())
                    return invalid ("--velocity", value);
                options.settings.velocity = *velocity;
            }
            else if (name == "--bpm")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --bpm" };
                const auto bpm = parseBpm (value);
                if (! bpm.has_value())
                    return invalid ("--bpm", value);
                options.settings.bpm = *bpm;
            }
            else if (name == "--out")
            {
                std::string value;
                if (! requireValue (value))
                    return CliParseResult { CliParseStatus::usageError, {}, "Missing value for --out" };
                if (value.empty())
                    return CliParseResult { CliParseStatus::usageError, {}, "Empty value for --out" };
                options.outputPath = value;
            }
            else
            {
                return CliParseResult { CliParseStatus::usageError, {}, "Unknown option: " + arg };
            }
        }
        else
        {
            if (positional.has_value())
                return CliParseResult { CliParseStatus::usageError, {}, "Unexpected positional argument" };
            positional = arg;
        }
    }

    options.progression = positional;
    return CliParseResult { CliParseStatus::ok, std::move (options), {} };
}

std::string helpText()
{
    return
        "Usage: chordtext-cli [options] [progression]\n"
        "\n"
        "Generates a MIDI file from a chord progression.\n"
        "\n"
        "Options:\n"
        "  --key <tonic>        Key tonic (C, C#, Db, D, ..., B). Default: C.\n"
        "  --mode <mode>        major, minor, dorian, phrygian, lydian,\n"
        "                       mixolydian, locrian, harmonic-minor, melodic-minor.\n"
        "                       Aliases: ionian, aeolian. Default: major.\n"
        "  --voicing <mode>     Voicing: voice-led, root-position, or root.\n"
        "                       Default: voice-led.\n"
        "  --range <range>      Pitch range: C2-C5, C3-C6, or C4-C7.\n"
        "                       Default: C3-C6.\n"
        "  --velocity <n>       Note velocity 1..127. Default: 100.\n"
        "  --bpm <n>            Tempo 4.0..999.0. Default: 120.\n"
        "  --json               Emit JSON output instead of a report.\n"
        "  --out <path>         Output MIDI file path.\n"
        "  --help               Show this help and exit.\n"
        "\n"
        "A single positional argument is the progression string. Use `--` to\n"
        "end option parsing.\n"
        "Chords: 1, 6, 4, 5 (scale triads); 2(7) (scale seventh).\n"
        "Add :N for duration in beats, for example 2(7):8.\n"
        "\n"
        "Exit codes:\n"
        "  0  success (including an empty progression)\n"
        "  1  progression parse errors\n"
        "  2  usage error, or a MIDI file could not be written\n";
}
}
