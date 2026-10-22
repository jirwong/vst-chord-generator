#include "Check.h"
#include "Cli/CliFile.h"
#include "Cli/CliOptions.h"
#include "Cli/CliReport.h"
#include "Cli/CliRun.h"
#include "Cli/CliSession.h"
#include "Cli/CliSettings.h"

#include <filesystem>
#include <fstream>

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

    CliSettings tableSettings;
    tableSettings.voicing = VoicingMode::rootPosition;
    tableSettings.rangeIndex = 0;

    const auto report = buildReport ("Cmaj7:2, Am7", tableSettings);
    CHECK (report.valid);
    CHECK (report.diagnostics.empty());
    CHECK (report.chords.size() == 2);
    CHECK (report.chords[0].source == "Cmaj7");
    CHECK (report.chords[0].symbol == "Cmaj7");
    CHECK (report.chords[0].degree == "Imaj7");
    CHECK ((report.chords[0].spelledTones
            == std::vector<std::string> { "C", "E", "G", "B" }));
    CHECK ((report.chords[0].voicing == std::vector<int> { 36, 40, 43, 47 }));
    CHECK ((report.chords[0].notes
            == std::vector<std::string> { "C", "E", "G", "B" }));
    CHECK (report.chords[0].startBeat == 0.0);
    CHECK (report.chords[0].durationBeats == 2.0);
    CHECK (report.chords[1].startBeat == 2.0);
    CHECK (report.chords[1].durationBeats == 4.0);
    CHECK (report.plan.lengthBeats == 6.0);
    CHECK (report.plan.events.size() == 16);

    const std::string expectedHuman =
        "Progression: Cmaj7:2, Am7\n"
        "Key: C major · Voicing: root-position · Range: C2-C5 · Velocity: 100\n"
        "\n"
        "Chord  Degree  Notes    Beats  MIDI\n"
        "Cmaj7  Imaj7   C E G B  2      36 40 43 47\n"
        "Am7    vi7     A C E G  4      45 48 52 55\n";
    CHECK (formatHuman (report) == expectedHuman);

    const auto invalid = buildReport ("C, H, C99", CliSettings {});
    CHECK (! invalid.valid);
    CHECK (invalid.diagnostics.size() == 2);
    const std::string expectedInvalid =
        "Progression: C, H, C99\n"
        "Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100\n"
        "\n"
        "2 errors:\n"
        "Error at character 3: unknown chord root (H)\n"
        "Error at character 6: unknown extension \"9\" (C99)\n";
    CHECK (formatHuman (invalid) == expectedInvalid);

    const auto empty = buildReport ("   ", CliSettings {});
    CHECK (empty.valid);
    CHECK (empty.chords.empty());
    CHECK (formatHuman (empty).find ("Progression: (empty)\n") == 0);
    CHECK (formatHuman (empty).find ("Empty progression (no chords).\n")
           != std::string::npos);

    const auto multiLine = buildReport ("C\n  Am7\tF", CliSettings {});
    CHECK (formatHuman (multiLine).find ("Progression: C Am7 F\n") == 0);

    const auto dim = buildReport ("C°", CliSettings {});
    CHECK (dim.valid);
    CHECK (dim.chords[0].symbol == "Cdim");
    CHECK (formatJson (dim).find ("\"symbol\": \"Cdim\"") != std::string::npos);
    const auto halfDim = buildReport ("Cø7", CliSettings {});
    CHECK (halfDim.valid);
    CHECK (halfDim.chords[0].symbol == "Cm7b5");

    const auto slash = buildReport ("C/E", CliSettings {});
    CHECK (slash.valid);
    CHECK ((slash.chords[0].voicing == std::vector<int> { 52, 60, 64, 67 }));
    CHECK ((slash.chords[0].notes
            == std::vector<std::string> { "E", "C", "E", "G" }));

    const auto fractions = buildReport ("Cmaj7:1.5, Am7:0.25, G7:0.3333333333333333",
                                        CliSettings {});
    CHECK (fractions.chords[0].durationBeats == 1.5);
    CHECK (fractions.chords[1].durationBeats == 0.25);
    const auto fractionsJson = formatJson (fractions);
    CHECK (fractionsJson.find ("\"durationBeats\": 1.5") != std::string::npos);
    CHECK (fractionsJson.find ("\"durationBeats\": 0.25") != std::string::npos);
    CHECK (fractionsJson.find ("\"durationBeats\": 0.3333333333333333")
           != std::string::npos);

    const auto humanJson = formatJson (report);
    CHECK (humanJson.find ("\"valid\": true") != std::string::npos);
    CHECK (humanJson.find ("\"key\": \"C\"") != std::string::npos);
    CHECK (humanJson.find ("\"voicing\": \"root-position\"") != std::string::npos);
    CHECK (humanJson.find ("\"range\": \"C2-C5\"") != std::string::npos);
    CHECK (humanJson.find ("\"velocity\": 100") != std::string::npos);
    CHECK (humanJson.find ("\"bpm\": 120") != std::string::npos);
    CHECK (humanJson.find ("\"source\": \"Cmaj7\"") != std::string::npos);
    CHECK (humanJson.find ("\"spelledTones\": [\"C\", \"E\", \"G\", \"B\"]")
           != std::string::npos);
    CHECK (humanJson.find ("\"notes\": [\"C\", \"E\", \"G\", \"B\"]")
           != std::string::npos);
    CHECK (humanJson.find ("\"voicing\": [36, 40, 43, 47]") != std::string::npos);
    CHECK (humanJson.find ("\"startBeat\": 0") != std::string::npos);
    CHECK (humanJson.find ("\"durationBeats\": 2") != std::string::npos);
    CHECK (humanJson.find ("\"midiFile\": null") != std::string::npos);
    CHECK (humanJson.find ("\"error\": null") != std::string::npos);
    CHECK (humanJson.back() == '\n');

    const auto invalidJson = formatJson (invalid);
    CHECK (invalidJson.find ("\"valid\": false") != std::string::npos);
    CHECK (invalidJson.find ("\"chords\": []") != std::string::npos);
    CHECK (invalidJson.find ("\"offset\": 3") != std::string::npos);
    CHECK (invalidJson.find ("\"token\": \"H\"") != std::string::npos);
    CHECK (invalidJson.find ("\"message\": \"unknown chord root\"")
           != std::string::npos);

    const auto escaped = buildReport (std::string ("a\"b\\c\nd"), CliSettings {});
    CHECK (formatJson (escaped).find ("\"text\": \"a\\\"b\\\\c\\nd\"")
           != std::string::npos);

    auto withFile = report;
    withFile.midiFile = "C:\\tmp\\x.mid";
    CHECK (formatJson (withFile).find ("\"midiFile\": \"C:\\\\tmp\\\\x.mid\"")
           != std::string::npos);
    withFile.error = "bad \"path\"";
    CHECK (formatJson (withFile).find ("\"error\": \"bad \\\"path\\\"\"")
           != std::string::npos);

    CliSession session;
    auto output = session.handleLine ("   ");
    CHECK (output.report.empty());
    CHECK (output.feedback.empty());
    CHECK (! output.quit);

    output = session.handleLine ("Cmaj7");
    CHECK (output.report.find ("Cmaj7") != std::string::npos);
    CHECK (output.feedback.empty());
    CHECK (session.lastValidText() == std::optional<std::string> { "Cmaj7" });

    output = session.handleLine (":key A");
    CHECK (output.report.empty());
    CHECK (output.feedback.find ("Key: A major") != std::string::npos);
    CHECK (session.settings().keyName() == "A");

    session.handleLine (":mode minor");
    output = session.handleLine ("i-iv-V");
    CHECK (output.report.find ("Am") != std::string::npos);
    CHECK (output.report.find ("Dm") != std::string::npos);
    CHECK (output.report.find ("G# B E") != std::string::npos);

    output = session.handleLine (":velocity 0");
    CHECK (output.feedback.find ("Error") != std::string::npos);
    CHECK (session.settings().velocity == 100);

    output = session.handleLine (":nope");
    CHECK (output.feedback.find ("Unknown command") != std::string::npos);

    output = session.handleLine (":help");
    CHECK (output.feedback.find (":key") != std::string::npos);
    CHECK (output.feedback.find (":export") != std::string::npos);

    session.handleLine (":json on");
    output = session.handleLine ("Cmaj7");
    CHECK (output.report.front() == '{');
    CHECK (output.report.find ("\"valid\": true") != std::string::npos);
    session.handleLine (":json off");

    output = session.handleLine (":q");
    CHECK (output.quit);

    const auto temp = std::filesystem::temp_directory_path()
                          / "chordtext-cli-session-test";
    std::filesystem::remove_all (temp);
    std::filesystem::create_directories (temp);

    CliSession exporter;
    output = exporter.handleLine (":export " + (temp / "nothing").string());
    CHECK (output.feedback.find ("Nothing to export") != std::string::npos);
    CHECK (! std::filesystem::exists (temp / "nothing.mid"));

    exporter.handleLine ("Cmaj7");
    output = exporter.handleLine (":export " + (temp / "loop").string());
    CHECK (output.feedback.find ("Wrote") != std::string::npos);
    CHECK (std::filesystem::exists (temp / "loop.mid"));
    std::filesystem::remove (temp / "loop.mid");

    output = exporter.handleLine ("C99");
    CHECK (output.report.find ("Error at character") != std::string::npos);
    CHECK (exporter.lastValidText() == std::optional<std::string> { "Cmaj7" });
    output = exporter.handleLine (":export " + (temp / "last").string());
    CHECK (std::filesystem::exists (temp / "last.mid"));
    std::filesystem::remove (temp / "last.mid");

    output = exporter.handleLine (":export " + (temp / "out file").string());
    CHECK (output.feedback.find ("Wrote") != std::string::npos);
    CHECK (std::filesystem::exists (temp / "out file.mid"));
    std::filesystem::remove (temp / "out file.mid");

    CliSession emptySession;
    emptySession.handleLine (",");
    output = emptySession.handleLine (":export " + (temp / "empty").string());
    CHECK (output.feedback.find ("Cannot write MIDI for an empty progression")
           != std::string::npos);
    CHECK (! std::filesystem::exists (temp / "empty.mid"));

    const auto blocker = temp / "blocker";
    {
        std::ofstream blockerFile (blocker);
        blockerFile << "x";
    }
    CliSession failing;
    failing.handleLine ("C");
    output = failing.handleLine (":export " + (blocker / "out").string());
    CHECK (output.feedback.find ("Error") != std::string::npos);
    CHECK (! std::filesystem::exists (blocker / "out.mid"));

    std::filesystem::remove_all (temp);

    CliOptions oneShot;
    auto run = runOneShot ("Cmaj7", oneShot);
    CHECK (run.exitCode == 0);
    CHECK (run.standardError.empty());
    CHECK (run.standardOutput.find ("Cmaj7") != std::string::npos);

    run = runOneShot ("C99", oneShot);
    CHECK (run.exitCode == 1);
    CHECK (run.standardOutput.find ("Error at character") != std::string::npos);

    run = runOneShot ("   ", oneShot);
    CHECK (run.exitCode == 0);
    CHECK (run.standardOutput.find ("Empty progression") != std::string::npos);

    oneShot.settings.json = true;
    run = runOneShot ("C99", oneShot);
    CHECK (run.exitCode == 1);
    CHECK (run.standardOutput.find ("\"valid\": false") != std::string::npos);
    CHECK (run.standardError.empty());
    oneShot.settings.json = false;

    const auto runTemp = std::filesystem::temp_directory_path()
                             / "chordtext-cli-run-test";
    std::filesystem::remove_all (runTemp);
    std::filesystem::create_directories (runTemp);

    oneShot.outputPath = (runTemp / "one-shot").string();
    run = runOneShot ("C, F, G", oneShot);
    CHECK (run.exitCode == 0);
    CHECK (run.standardOutput.find ("Wrote ") != std::string::npos);
    CHECK (std::filesystem::exists (runTemp / "one-shot.mid"));
    std::filesystem::remove (runTemp / "one-shot.mid");

    oneShot.outputPath = (runTemp / "empty").string();
    run = runOneShot ("   ", oneShot);
    CHECK (run.exitCode == 2);
    CHECK (run.standardError.find ("Cannot write MIDI for an empty progression")
           != std::string::npos);
    CHECK (! std::filesystem::exists (runTemp / "empty.mid"));

    oneShot.outputPath = (runTemp / "invalid").string();
    run = runOneShot ("C99", oneShot);
    CHECK (run.exitCode == 1);
    CHECK (! std::filesystem::exists (runTemp / "invalid.mid"));

    const auto runBlocker = runTemp / "blocker";
    {
        std::ofstream blockerFile (runBlocker);
        blockerFile << "x";
    }
    oneShot.outputPath = (runBlocker / "out").string();
    run = runOneShot ("C", oneShot);
    CHECK (run.exitCode == 2);
    CHECK (! run.standardError.empty());
    CHECK (! std::filesystem::exists (runBlocker / "out.mid"));

    CliOptions jsonOptions;
    jsonOptions.settings.json = true;
    jsonOptions.outputPath = (runTemp / "json").string();
    run = runOneShot ("C", jsonOptions);
    CHECK (run.exitCode == 0);
    CHECK (run.standardError.empty());
    CHECK (run.standardOutput.find ("\"midiFile\": \"") != std::string::npos);
    CHECK (std::filesystem::exists (runTemp / "json.mid"));

    jsonOptions.outputPath = (runBlocker / "out").string();
    run = runOneShot ("C", jsonOptions);
    CHECK (run.exitCode == 2);
    CHECK (run.standardError.empty());
    CHECK (run.standardOutput.find ("\"error\": \"") != std::string::npos);

    std::filesystem::remove_all (runTemp);

    return reportAndExit();
}
