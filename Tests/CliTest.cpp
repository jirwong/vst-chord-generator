#include "Check.h"
#include "Cli/CliFile.h"
#include "Cli/CliOptions.h"
#include "Cli/CliReport.h"
#include "Cli/CliRun.h"
#include "Cli/CliSession.h"
#include "Cli/CliSettings.h"
#include "Theory/Length.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace chordtext;
using namespace chordtext::cli;

namespace
{
int readMidiU32 (const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (int (bytes[offset]) << 24) | (int (bytes[offset + 1]) << 16)
         | (int (bytes[offset + 2]) << 8) | int (bytes[offset + 3]);
}

int readMidiVlq (const std::vector<std::uint8_t>& bytes, std::size_t& offset)
{
    int value = 0;
    for (;;)
    {
        const auto byte = bytes[offset++];
        value = (value << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0)
            return value;
    }
}

std::vector<int> readMidiNoteOnPitches (const std::filesystem::path& path)
{
    std::ifstream input (path, std::ios::binary);
    const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> (input),
                                           std::istreambuf_iterator<char>() };
    std::vector<int> result;
    if (bytes.size() < 22)
        return result;

    auto offset = std::size_t (8 + readMidiU32 (bytes, 4) + 8);
    const auto trackEnd = offset - 8 + std::size_t (8 + readMidiU32 (bytes, offset - 4));
    int runningStatus = 0;
    while (offset < trackEnd)
    {
        readMidiVlq (bytes, offset);
        auto status = bytes[offset++];
        if (status < 0x80)
        {
            --offset;
            status = static_cast<std::uint8_t> (runningStatus);
        }
        else
        {
            runningStatus = status;
        }

        if (status == 0xFF)
        {
            ++offset;
            offset += static_cast<std::size_t> (readMidiVlq (bytes, offset));
            continue;
        }

        const auto kind = status & 0xF0;
        const auto pitch = int (bytes[offset++]);
        const auto velocity = (kind == 0xC0 || kind == 0xD0)
            ? 0 : int (bytes[offset++]);
        if (kind == 0x90 && velocity > 0)
            result.push_back (pitch);
    }

    return result;
}

std::vector<std::uint8_t> readMidiBytes (const std::filesystem::path& path)
{
    std::ifstream input (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char>()};
}

bool containsBytes (const std::vector<std::uint8_t>& bytes,
                    std::initializer_list<std::uint8_t> pattern)
{
    return std::search (bytes.begin(), bytes.end(), pattern.begin(), pattern.end())
        != bytes.end();
}

std::vector<int> readMidiNoteOffTicks (const std::filesystem::path& path)
{
    const auto bytes = readMidiBytes (path);
    std::vector<int> result;
    if (bytes.size() < 22)
        return result;

    auto offset = std::size_t (8 + readMidiU32 (bytes, 4) + 8);
    const auto trackEnd = offset - 8 + std::size_t (8 + readMidiU32 (bytes, offset - 4));
    auto tick = 0;
    auto runningStatus = 0;
    while (offset < trackEnd)
    {
        tick += readMidiVlq (bytes, offset);
        auto status = bytes[offset++];
        if (status < 0x80)
        {
            --offset;
            status = static_cast<std::uint8_t> (runningStatus);
        }
        else
        {
            runningStatus = status;
        }

        if (status == 0xFF)
        {
            ++offset;
            offset += static_cast<std::size_t> (readMidiVlq (bytes, offset));
            continue;
        }

        const auto kind = status & 0xF0;
        ++offset;
        const auto velocity = (kind == 0xC0 || kind == 0xD0) ? 0 : int (bytes[offset++]);
        if (kind == 0x80 || (kind == 0x90 && velocity == 0))
            result.push_back (tick);
    }

    return result;
}
}

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
    CHECK (defaults.bassOctaves == 0);
    CHECK (formatLength (defaults.defaultLength) == "4beats");
    CHECK ((defaults.timeSignature == TimeSignature {4, 4}));
    CHECK (defaults.keyName() == "C");
    CHECK (defaults.modeName() == "major");
    CHECK (defaults.voicingName() == "voice-led");
    CHECK (defaults.rangeName() == "C3-C6");
    CHECK (formatSettingsLine (defaults)
           == "Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off"
              " · Default length: 4beats · Time signature: 4/4");

    const auto midiSettings = defaults.midiFileSettings();
    CHECK (midiSettings.bpm == 120.0);
    CHECK (midiSettings.includeTempoAndTimeSignature);
    CHECK (midiSettings.trackName == "Chord Text");
    CHECK (midiSettings.ticksPerQuarterNote == 960);
    CHECK ((midiSettings.signature == TimeSignature {4, 4}));

    CHECK (parseTonic ("db")->name() == "Db");
    CHECK (! parseTonic ("H").has_value());
    CHECK (parseMode ("MINOR") == std::optional<Mode> { Mode::minor });
    CHECK (! parseMode ("unknown-scale").has_value());
    CHECK (parseMode ("DORIAN") == std::optional<Mode> { Mode::dorian });
    CHECK (parseMode ("Ionian") == std::optional<Mode> { Mode::major });
    CHECK (parseMode ("Aeolian") == std::optional<Mode> { Mode::minor });
    for (const auto scaleMode : allModes())
    {
        const std::string name (chordtext::modeName (scaleMode));
        const auto options = parseOptions ({"--mode", name, "1, 2(7)"});
        CHECK (options.status == CliParseStatus::ok);
        CHECK (options.options.settings.mode == scaleMode);
        CHECK (options.options.settings.modeName() == name);
        CliSession modalSession;
        const auto feedback = modalSession.handleLine (":mode " + name);
        CHECK (feedback.feedback.find ("Error") == std::string::npos);
        CHECK (modalSession.settings().mode == scaleMode);
    }
    CliSettings modalSettings;
    modalSettings.mode = Mode::dorian;
    const auto numericReport = buildReport ("3(7)", modalSettings);
    CHECK (numericReport.valid);
    CHECK (numericReport.chords[0].source == "3(7)");
    CHECK (numericReport.chords[0].symbol == "Ebmaj7");
    CHECK (numericReport.chords[0].degree == "IIImaj7");
    CHECK (formatJson (numericReport).find ("\"mode\": \"dorian\"") != std::string::npos);
    modalSettings.tonic = {'A', 0};
    modalSettings.mode = Mode::harmonicMinor;
    const auto harmonicReport = buildReport ("3(7)", modalSettings);
    CHECK (harmonicReport.valid);
    CHECK (harmonicReport.chords[0].symbol == "Cmaj7#5");
    CHECK ((harmonicReport.chords[0].spelledTones == std::vector<std::string> {"C", "E", "G#", "B"}));
    CHECK (formatJson (harmonicReport).find ("\"mode\": \"harmonic-minor\"") != std::string::npos);
    CHECK (! buildReport ("1645", modalSettings).valid);
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
    CHECK (parseBassOctaves ("off") == std::optional<int> { 0 });
    CHECK (parseBassOctaves ("OFF") == std::optional<int> { 0 });
    CHECK (parseBassOctaves ("1") == std::optional<int> { 1 });
    CHECK (parseBassOctaves ("2") == std::optional<int> { 2 });
    CHECK (parseBassOctaves ("3") == std::optional<int> { 3 });
    CHECK (! parseBassOctaves ("0").has_value());
    CHECK (! parseBassOctaves ("4").has_value());
    CHECK (! parseBassOctaves ("-1").has_value());

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

    parsed = parseOptions ({ "--bass", "2", "C" });
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (parsed.options.settings.bassOctaves == 2);
    parsed = parseOptions ({ "--bass=off", "C" });
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (parsed.options.settings.bassOctaves == 0);

    parsed = parseOptions ({ "--default-length", "1/8D", "--time-signature=6/8", "C" });
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (formatLength (parsed.options.settings.defaultLength) == "1/8D");
    CHECK ((parsed.options.settings.timeSignature == TimeSignature {6, 8}));
    parsed = parseOptions ({ "--default-length=5/16", "C" });
    CHECK (parsed.status == CliParseStatus::ok);
    CHECK (formatLength (parsed.options.settings.defaultLength) == "5/16");

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
        { "--bpm", "3.9" },
        { "--bass", "0" },
        { "--bass", "4" },
        { "--bass" },
        { "--default-length", "1/3" },
        { "--default-length" },
        { "--time-signature", "4/3" },
        { "--time-signature" }
    };

    for (const auto& args : usageErrors)
    {
        const auto result = parseOptions (args);
        CHECK (result.status == CliParseStatus::usageError);
        CHECK (! result.error.empty());
    }

    parsed = parseOptions ({ "-h" });
    CHECK (parsed.status == CliParseStatus::usageError);
    CHECK (parsed.error.find ("Unknown option") != std::string::npos);

    parsed = parseOptions ({ "-k", "A", "i-iv-V" });
    CHECK (parsed.status == CliParseStatus::usageError);

    parsed = parseOptions ({ "--out=", "C" });
    CHECK (parsed.status == CliParseStatus::usageError);

    parsed = parseOptions ({ "--out", "", "C" });
    CHECK (parsed.status == CliParseStatus::usageError);

    CHECK (! helpText().empty());
    CHECK (helpText().find ("--key") != std::string::npos);
    CHECK (helpText().find ("--out") != std::string::npos);
    CHECK (helpText().find ("--bass") != std::string::npos);
    CHECK (helpText().find ("--default-length") != std::string::npos);
    CHECK (helpText().find ("--time-signature") != std::string::npos);
    CHECK (helpText().find ("chordtext-cli") != std::string::npos);
    CHECK (helpText().find ("  1  progression parse errors") != std::string::npos);
    CHECK (helpText().find ("  2  usage error") != std::string::npos);

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

    CliSettings lengthSettings = tableSettings;
    const auto dottedEighth = parseLength ("1/8D");
    CHECK (dottedEighth.value.has_value());
    if (dottedEighth.value)
        lengthSettings.defaultLength = *dottedEighth.value;
    lengthSettings.timeSignature = {6, 8};
    const auto lengthReport = buildReport ("C, Dm:2, G:1bar, [1]:1/8T", lengthSettings);
    CHECK (lengthReport.valid);
    CHECK (lengthReport.chords.size() == 4);
    if (lengthReport.chords.size() == 4)
    {
        CHECK (lengthReport.chords[0].durationBeats == 0.75);
        CHECK (lengthReport.chords[1].durationBeats == 2.0);
        CHECK (lengthReport.chords[2].durationBeats == 3.0);
        CHECK (lengthReport.chords[3].durationBeats == 1.0 / 3.0);
        CHECK (lengthReport.chords[0].startBeat == 0.0);
        CHECK (lengthReport.chords[1].startBeat == 0.75);
        CHECK (lengthReport.chords[2].startBeat == 2.75);
        CHECK (lengthReport.chords[3].startBeat == 5.75);
        CHECK (lengthReport.chords[0].lengthExpression == "1/8D");
        CHECK (lengthReport.chords[0].usesDefaultLength);
        CHECK (lengthReport.chords[2].lengthExpression == "1bar");
        CHECK (! lengthReport.chords[2].usesDefaultLength);
    }
    const auto lengthJson = formatJson (lengthReport);
    CHECK (lengthJson.find ("\"defaultLength\": \"1/8D\"") != std::string::npos);
    CHECK (lengthJson.find ("\"timeSignature\": {\"numerator\": 6, \"denominator\": 8}")
           != std::string::npos);
    CHECK (lengthJson.find ("\"lengthExpression\": \"1bar\"") != std::string::npos);
    CHECK (lengthJson.find ("\"usesDefaultLength\": true") != std::string::npos);

    const std::string expectedHuman =
        "Progression: Cmaj7:2, Am7\n"
        "Key: C major · Voicing: root-position · Range: C2-C5 · Velocity: 100 · Bass: Off"
        " · Default length: 4beats · Time signature: 4/4\n"
        "\n"
        "Chord  Degree  Notes    Length  Beats  MIDI\n"
        "Cmaj7  Imaj7   C E G B  2beats  2      36 40 43 47\n"
        "Am7    vi7     A C E G  4beats  4      45 48 52 55\n";
    CHECK (formatHuman (report) == expectedHuman);

    const auto invalid = buildReport ("C, H, C99", CliSettings {});
    CHECK (! invalid.valid);
    CHECK (invalid.diagnostics.size() == 2);
    const std::string expectedInvalid =
        "Progression: C, H, C99\n"
        "Key: C major · Voicing: voice-led · Range: C3-C6 · Velocity: 100 · Bass: Off"
        " · Default length: 4beats · Time signature: 4/4\n"
        "\n"
        "2 errors:\n"
        "Error at character 3: unknown chord root (H)\n"
        "Error at character 6: unknown extension \"9\" (C99)\n";
    CHECK (formatHuman (invalid) == expectedInvalid);

    CliSettings bassSettings = tableSettings;
    bassSettings.bassOctaves = 1;
    const auto bassReport = buildReport ("Cmaj7", bassSettings);
    CHECK (bassReport.valid);
    CHECK (bassReport.chords[0].bassMidi == std::optional<int> { 24 });
    CHECK (bassReport.chords[0].bassNote == std::optional<std::string> { "C1" });
    CHECK ((bassReport.chords[0].voicing == std::vector<int> { 24, 36, 40, 43, 47 }));
    CHECK ((bassReport.chords[0].notes
            == std::vector<std::string> { "C", "C", "E", "G", "B" }));
    CHECK (formatHuman (bassReport).find ("C1") != std::string::npos);
    CHECK (formatHuman (bassReport).find ("Bass") != std::string::npos);
    const auto bassJson = formatJson (bassReport);
    CHECK (bassJson.find ("\"bassOctaves\": 1") != std::string::npos);
    CHECK (bassJson.find ("\"bassMidi\": 24") != std::string::npos);
    CHECK (bassJson.find ("\"bassNote\": \"C1\"") != std::string::npos);
    const auto invalidBass = buildReport ("[C-1]", bassSettings);
    CHECK (! invalidBass.valid);
    CHECK (invalidBass.chords.empty());
    CHECK (invalidBass.plan.events.empty());
    CHECK (! invalidBass.diagnostics.empty());

    const auto customReport = buildReport ("[1,2,4,5]:2", tableSettings);
    CHECK (customReport.valid);
    CHECK (customReport.chords[0].symbol == "C custom");
    CHECK (customReport.chords[0].degree == "custom");
    CHECK ((customReport.chords[0].notes
            == std::vector<std::string> { "C2", "D2", "F2", "G2" }));
    CHECK (customReport.chords[0].durationBeats == 2.0);

    const auto enharmonicCustomReport = buildReport ("[C#4,Db5]", tableSettings);
    CHECK (enharmonicCustomReport.valid);
    CHECK ((enharmonicCustomReport.chords[0].notes
            == std::vector<std::string> { "C#4", "Db5" }));

    const auto singleError = buildReport ("C, H", CliSettings {});
    CHECK (formatHuman (singleError).find ("1 error:\n") != std::string::npos);

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
    CHECK (humanJson.find ("\"bassOctaves\": 0") != std::string::npos);
    CHECK (humanJson.find ("\"bassMidi\": null") != std::string::npos);
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

    output = session.handleLine (":bass 2");
    CHECK (output.feedback.find ("Bass: 2 octaves down") != std::string::npos);
    CHECK (session.settings().bassOctaves == 2);
    output = session.handleLine (":bass 4");
    CHECK (output.feedback.find ("Error") != std::string::npos);
    CHECK (session.settings().bassOctaves == 2);

    output = session.handleLine (":nope");
    CHECK (output.feedback.find ("Unknown command") != std::string::npos);

    output = session.handleLine (":help");
    CHECK (output.feedback.find (":key") != std::string::npos);
    CHECK (output.feedback.find (":export") != std::string::npos);
    CHECK (output.feedback.find (":bass") != std::string::npos);

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

    CliSession lengthExporter;
    lengthExporter.handleLine ("C");
    output = lengthExporter.handleLine (":default-length 1/8D");
    CHECK (output.feedback.find ("1/8D") != std::string::npos);
    output = lengthExporter.handleLine (":time-signature 6/8");
    CHECK (output.feedback.find ("6/8") != std::string::npos);
    output = lengthExporter.handleLine ("H");
    CHECK (output.report.find ("Error at character") != std::string::npos);
    output = lengthExporter.handleLine (":export " + (temp / "lengths").string());
    CHECK (output.feedback.find ("Wrote") != std::string::npos);
    const auto lengthFile = temp / "lengths.mid";
    const auto lengthBytes = readMidiBytes (lengthFile);
    CHECK (containsBytes (lengthBytes, {0xFF, 0x58, 0x04, 0x06, 0x03, 0x18, 0x08}));
    CHECK ((readMidiNoteOffTicks (lengthFile) == std::vector<int> {720, 720, 720}));
    output = lengthExporter.handleLine (":time-signature 4/3");
    CHECK (output.feedback.find ("Error") != std::string::npos);
    CHECK ((lengthExporter.settings().timeSignature == TimeSignature {6, 8}));

    exporter.handleLine ("[C-1,1,3,5]");
    exporter.handleLine (":bass 1");
    output = exporter.handleLine (":export " + (temp / "bounds").string());
    CHECK (output.feedback.find ("Cannot export") != std::string::npos);
    CHECK (! std::filesystem::exists (temp / "bounds.mid"));
    exporter.handleLine (":bass off");

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

    const auto originalCwd = std::filesystem::current_path();
    const auto cwdTemp = std::filesystem::temp_directory_path()
                             / "chordtext-cli-export-empty-test";
    std::filesystem::remove_all (cwdTemp);
    std::filesystem::create_directories (cwdTemp);
    std::filesystem::current_path (cwdTemp);

    CliSession emptyPath;
    emptyPath.handleLine ("Cmaj7");
    output = emptyPath.handleLine (":export");
    CHECK (output.feedback.find ("missing path") != std::string::npos);

    bool anyMidInCwd = false;
    for (const auto& entry : std::filesystem::directory_iterator (cwdTemp))
    {
        if (entry.path().extension() == ".mid")
        {
            anyMidInCwd = true;
            break;
        }
    }
    CHECK (! anyMidInCwd);

    std::filesystem::current_path (originalCwd);
    std::filesystem::remove_all (cwdTemp);

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

    oneShot.settings.bassOctaves = 1;
    run = runOneShot ("[1,3,5,F]", oneShot);
    CHECK (run.exitCode == 0);
    CHECK (run.standardOutput.find ("C custom") != std::string::npos);
    oneShot.settings = CliSettings {};

    const auto runTemp = std::filesystem::temp_directory_path()
                             / "chordtext-cli-run-test";
    std::filesystem::remove_all (runTemp);
    std::filesystem::create_directories (runTemp);

    oneShot.settings.bassOctaves = 1;
    oneShot.outputPath = (runTemp / "invalid-bass").string();
    run = runOneShot ("[C-1]", oneShot);
    CHECK (run.exitCode == 1);
    CHECK (! std::filesystem::exists (runTemp / "invalid-bass.mid"));
    oneShot.settings = CliSettings {};

    CliOptions parityOptions;
    parityOptions.settings.bassOctaves = 1;
    parityOptions.outputPath = (runTemp / "parity").string();
    const auto parityText = "[C5,E4,G4]:2";
    const auto parityReport = buildReport (parityText, parityOptions.settings);
    run = runOneShot (parityText, parityOptions);
    CHECK (run.exitCode == 0);
    CHECK ((parityReport.chords[0].voicing == std::vector<int> { 60, 64, 67, 72 }));
    CHECK ((readMidiNoteOnPitches (runTemp / "parity.mid")
            == parityReport.chords[0].voicing));
    std::filesystem::remove (runTemp / "parity.mid");

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

    const auto preservedFile = runTemp / "preserved.mid";
    {
        std::ofstream preservedOutput (preservedFile, std::ios::binary);
        preservedOutput << "keep";
    }
    oneShot.outputPath = (runTemp / "preserved").string();
    run = runOneShot ("C:0.0001", oneShot);
    CHECK (run.exitCode == 2);
    {
        std::ifstream preservedInput (preservedFile, std::ios::binary);
        CHECK (std::string (std::istreambuf_iterator<char> (preservedInput), {}) == "keep");
    }

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
