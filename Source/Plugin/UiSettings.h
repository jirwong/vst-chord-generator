#pragma once

#include <juce_core/juce_core.h>

// UI-only preferences. Read on editor open; never read from the audio callback.
// Each object reads the file afresh, including changes from another DAW process.
class UiSettings
{
public:
    UiSettings();
    explicit UiSettings (juce::File file);

    static juce::File defaultFile();
    static bool isSupportedScale (int percent);
    int loadUiScalePercent() const;
    juce::Result saveUiScalePercent (int percent) const;

private:
    juce::File settingsFile;
};
