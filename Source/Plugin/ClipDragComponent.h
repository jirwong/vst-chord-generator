#pragma once

#include "Theory/Length.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class ClipDragComponent final : public juce::Component
{
public:
    std::function<juce::File()> createClipFile;
    std::function<void (const juce::String&)> onStatus;

    void setLoopInfo (double beats, bool enabled,
                      chordtext::TimeSignature signature = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    double loopBeats = 0.0;
    chordtext::TimeSignature loopSignature;
    bool canDrag = false;
    bool dragStarted = false;
    juce::Point<int> dragStart;
};
