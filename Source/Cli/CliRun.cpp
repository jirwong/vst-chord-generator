#include "Cli/CliRun.h"

#include "Cli/CliFile.h"
#include "Cli/CliReport.h"
#include "Theory/MidiFileWriter.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace chordtext::cli
{
CliRunResult runOneShot (std::string_view text, const CliOptions& options)
{
    auto report = buildReport (text, options.settings);

    int exitCode = report.valid ? 0 : 1;

    if (report.valid && options.outputPath.has_value())
    {
        if (report.plan.events.empty())
        {
            report.error = "Cannot write MIDI for an empty progression.";
            exitCode = 2;
        }
        else
        {
            const auto path = resolveMidiPath (*options.outputPath);
            std::string error;

            if (chordtext::writeMidiFile (report.plan, path, options.settings.midiFileSettings(),
                                          error))
            {
                report.midiFile = path.u8string();
            }
            else
            {
                std::error_code ignored;
                std::filesystem::remove (path, ignored);
                report.error = error;
                exitCode = 2;
            }
        }
    }

    CliRunResult result;
    result.exitCode = exitCode;
    result.standardOutput = options.settings.json ? formatJson (report) : formatHuman (report);

    if (! options.settings.json && report.error.has_value())
        result.standardError = "Error: " + *report.error + "\n";

    return result;
}
}
