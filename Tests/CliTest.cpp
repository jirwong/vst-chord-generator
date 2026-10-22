#include "Check.h"
#include "Cli/CliFile.h"
#include "Cli/CliOptions.h"
#include "Cli/CliSettings.h"

using namespace chordtext;
using namespace chordtext::cli;

int main()
{
    const CliSettings defaults;
    CHECK (defaults.key().tonic().name() == "C");
    CHECK (defaults.mode == Mode::major);
    CHECK (defaults.voicing == VoicingMode::voiceLed);
    CHECK (defaults.rangeIndex == 1);
    CHECK (defaults.velocity == 100);
    CHECK (defaults.bpm == 120.0);
    CHECK (! defaults.json);
    CHECK (defaults.keyName() == "C");
    CHECK (defaults.modeName() == "major");
    CHECK (defaults.voicingName() == "voice-led");
    CHECK (defaults.rangeName() == "C3-C6");
    CHECK (formatSettingsLine (defaults)
           == "Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100");

    const auto midiSettings = defaults.midiFileSettings();
    CHECK (midiSettings.bpm == 120.0);
    CHECK (midiSettings.includeTempoAndTimeSignature);
    CHECK (midiSettings.trackName == "Chord Text");
    CHECK (midiSettings.ticksPerQuarterNote == 960);

    CHECK (parseTonic ("db")->name() == "Db");
    CHECK (! parseTonic ("H").has_value());
    CHECK (parseMode ("MINOR") == std::optional<Mode> { Mode::minor });
    CHECK (! parseMode ("dorian").has_value());
    CHECK (parseVoicing ("ROOT-POSITION")
           == std::optional<VoicingMode> { VoicingMode::rootPosition });
    CHECK (parseVoicing ("root")
           == std::optional<VoicingMode> { VoicingMode::rootPosition });
    CHECK (! parseVoicing ("close").has_value());
    CHECK (parseRangeIndex ("c2-c5") == std::optional<int> { 0 });
    CHECK (parseRangeIndex ("C4-C7") == std::optional<int> { 2 });
    CHECK (! parseRangeIndex ("C1-C2").has_value());
    CHECK (parseVelocity ("127") == std::optional<int> { 127 });
    CHECK (! parseVelocity ("0").has_value());
    CHECK (! parseVelocity ("128").has_value());
    CHECK (! parseVelocity ("12x").has_value());
    CHECK (parseBpm ("96.5") == std::optional<double> { 96.5 });
    CHECK (! parseBpm ("3.9").has_value());
    CHECK (! parseBpm ("1000").has_value());
    CHECK (! parseBpm ("nan").has_value());

    auto parsed = parseOptions ({});
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (! parsed.options.progression.has_value());
    CHECK (! parsed.options.outputPath.has_value());
    CHECK (parsed.options.settings.keyName() == "C");

    parsed = parseOptions ({ "--key", "A", "--mode=minor", "i-iv-V" });
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (parsed.options.settings.keyName() == "A");
    CHECK (parsed.options.settings.mode == Mode::minor);
    CHECK (parsed.options.progression == std::optional<std::string> { "i-iv-V" });

    parsed = parseOptions ({ "--key=C#", "--range", "C4-C7", "--velocity=64",
                             "--bpm=90", "--json", "--out", "loop" });
    CHECK (parsed.options.settings.keyName() == "C#");
    CHECK (parsed.options.settings.rangeIndex == 2);
    CHECK (parsed.options.settings.velocity == 64);
    CHECK (parsed.options.settings.bpm == 90.0);
    CHECK (parsed.options.settings.json);
    CHECK (parsed.options.outputPath == std::optional<std::string> { "loop" });

    parsed = parseOptions ({ "--key", "C", "--key", "D" });
    CHECK (parsed.options.settings.keyName() == "D");

    parsed = parseOptions ({ "--", "-C" });
    CHECK (parsed.options.progression == std::optional<std::string> { "-C" });

    parsed = parseOptions ({ "--help", "--key", "H" });
    CHECK (parsed.status == CliParseStatus::help);

    const std::vector<std::vector<std::string>> usageErrors {
        { "--key", "H" },
        { "--key" },
        { "--mystery" },
        { "C", "D" },
        { "--json=on" },
        { "--velocity", "0" },
        { "--bpm", "3.9" }
    };

    for (const auto& args : usageErrors)
    {
        const auto result = parseOptions (args);
        CHECK (result.status == CliParseStatus::usageError);
        CHECK (! result.error.empty());
    }

    CHECK (! helpText().empty());
    CHECK (helpText().find ("--key") != std::string::npos);
    CHECK (helpText().find ("--out") != std::string::npos);

    CHECK (resolveMidiPath ("loop").filename().u8string() == "loop.mid");
    CHECK (resolveMidiPath ("loop").is_absolute());
    CHECK (resolveMidiPath ("loop.txt").filename().u8string() == "loop.mid");
    CHECK (resolveMidiPath ("loop.MID").filename().u8string() == "loop.MID");

    return reportAndExit();
}
