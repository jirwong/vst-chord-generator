#include "Cli/CliOptions.h"
#include "Cli/CliRun.h"
#include "Cli/CliSession.h"
#include "Cli/CliSettings.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <cstdio>
    #include <io.h>
    #include <windows.h>
#endif

namespace chordtext::cli
{
namespace
{
int runApplication (const std::vector<std::string>& args)
{
    const auto parsed = parseOptions (args);

    if (parsed.status == CliParseStatus::help)
    {
        std::cout << helpText();
        return 0;
    }

    if (parsed.status == CliParseStatus::usageError)
    {
        std::cerr << parsed.error << "\n";
        return 2;
    }

#ifdef _WIN32
    const bool interactive = _isatty (_fileno (stdin)) != 0;
#else
    const bool interactive = false;
#endif

    std::string text;

    if (! parsed.options.progression.has_value())
    {
        if (interactive)
        {
            if (parsed.options.outputPath.has_value())
            {
                std::cerr << "--out requires a progression or piped input. Use :export in the REPL.\n";
                return 2;
            }

            std::cout << "Chord Text CLI — :help for commands\n";
            std::cout << formatSettingsLine (parsed.options.settings) << "\n\n";

            CliSession session (parsed.options.settings);

            for (;;)
            {
                std::cout << "chord> " << std::flush;

                std::string line;
                if (! std::getline (std::cin, line))
                    break;

                const auto output = session.handleLine (line);

                if (! output.report.empty())
                    std::cout << output.report << std::flush;
                if (! output.feedback.empty())
                    std::cerr << output.feedback << std::flush;

                if (output.quit)
                    break;
            }

            return 0;
        }

        std::ostringstream buffer;
        buffer << std::cin.rdbuf();

        if (std::cin.bad())
        {
            std::cerr << "Cannot read standard input.\n";
            return 2;
        }

        text = buffer.str();
    }
    else
    {
        text = *parsed.options.progression;
    }

    const auto result = runOneShot (text, parsed.options);
    std::cout << result.standardOutput << std::flush;
    std::cerr << result.standardError << std::flush;
    return result.exitCode;
}
}
}

#ifdef _WIN32
int wmain (int argc, wchar_t** argv)
{
    SetConsoleCP (CP_UTF8);
    SetConsoleOutputCP (CP_UTF8);

    std::vector<std::string> args;
    args.reserve (static_cast<std::size_t> (argc > 0 ? argc - 1 : 0));

    for (int i = 1; i < argc; ++i)
    {
        const auto length = WideCharToMultiByte (CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            continue;

        std::string converted (static_cast<std::size_t> (length), '\0');
        WideCharToMultiByte (CP_UTF8, 0, argv[i], -1, converted.data(), length, nullptr, nullptr);
        converted.resize (static_cast<std::size_t> (length - 1));
        args.push_back (std::move (converted));
    }

    return chordtext::cli::runApplication (args);
}
#else
int main (int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve (static_cast<std::size_t> (argc > 0 ? argc - 1 : 0));

    for (int i = 1; i < argc; ++i)
        args.emplace_back (argv[i]);

    return chordtext::cli::runApplication (args);
}
#endif
