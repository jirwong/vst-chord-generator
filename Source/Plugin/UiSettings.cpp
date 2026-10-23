#include "UiSettings.h"

#include <utility>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

UiSettings::UiSettings() : UiSettings (defaultFile()) {}
UiSettings::UiSettings (juce::File file) : settingsFile (std::move (file)) {}

juce::File UiSettings::defaultFile()
{
    const auto localAppData = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
    if (localAppData.isEmpty())
        return {};
    return juce::File (localAppData).getChildFile ("Chordscript/ui-settings.json");
}

bool UiSettings::isSupportedScale (int percent)
{
    return percent == 75 || percent == 100 || percent == 125 || percent == 150;
}

int UiSettings::loadUiScalePercent() const
{
    if (settingsFile == juce::File() || ! settingsFile.existsAsFile())
        return 100;
    const auto object = juce::JSON::parse (settingsFile.loadFileAsString());
    const auto value = object["uiScalePercent"];
    if (! value.isInt() && ! value.isInt64())
        return 100;
    const auto percent = static_cast<juce::int64> (value);
    return percent == 75 || percent == 100 || percent == 125 || percent == 150
        ? static_cast<int> (percent) : 100;
}

juce::Result UiSettings::saveUiScalePercent (int percent) const
{
    if (! isSupportedScale (percent) || settingsFile == juce::File())
        return juce::Result::fail ("Invalid UI scale or settings file");
    if (settingsFile.getParentDirectory().createDirectory().failed() || settingsFile.isDirectory())
        return juce::Result::fail ("Cannot create UI settings file");

    juce::var object (new juce::DynamicObject());
    object.getDynamicObject()->setProperty ("uiScalePercent", percent);
    const auto json = juce::JSON::toString (object) + "\n";
    const juce::TemporaryFile temporary (settingsFile);
    {
        juce::FileOutputStream output (temporary.getFile());
        if (! output.openedOk() || ! output.write (json.toRawUTF8(), json.getNumBytesAsUTF8()))
            return juce::Result::fail ("Cannot write UI settings file");
        output.flush();
        if (output.getStatus().failed())
            return output.getStatus();
    }
    // One same-volume rename handles both first save and replacement. JUCE's
    // absent-target move path deletes the destination, which races other DAWs.
    if (MoveFileExW (temporary.getFile().getFullPathName().toWideCharPointer(),
                     settingsFile.getFullPathName().toWideCharPointer(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        return juce::Result::fail ("Cannot replace UI settings file (Windows error "
                                   + juce::String (static_cast<int> (GetLastError())) + ")");
    return juce::Result::ok();
}
