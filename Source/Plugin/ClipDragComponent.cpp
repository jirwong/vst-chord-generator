#include "ClipDragComponent.h"

#include <cmath>

namespace
{
const auto clipBackgroundColour = juce::Colour (0xff2b3f56);
const auto clipOutlineColour = juce::Colour (0xff4d7ba6);
const auto clipAccentColour = juce::Colour (0xff7fb2e5);
const auto clipTextColour = juce::Colour (0xffe4e4e6);
const auto clipDimTextColour = juce::Colour (0xffb0b0b4);
const auto clipErrorTextColour = juce::Colour (0xffe07070);

juce::String formatNumber (double value)
{
    if (std::abs (value - std::round (value)) < 1e-9)
        return juce::String (static_cast<int> (std::lround (value)));

    auto text = juce::String (value, 2);

    while (text.endsWithChar ('0'))
        text = text.dropLastCharacters (1);

    if (text.endsWithChar ('.'))
        text = text.dropLastCharacters (1);

    return text;
}

juce::String describeLoop (double beats)
{
    if (beats <= 0.0)
        return "No loop";

    return formatNumber (beats) + " beats \xc2\xb7 " + formatNumber (beats / 4.0)
           + " bars (4/4)";
}
} // namespace

void ClipDragComponent::setLoopInfo (double beats, bool enabled)
{
    if (loopBeats == beats && canDrag == enabled && isEnabled() == enabled)
        return;

    loopBeats = beats;
    canDrag = enabled;
    setEnabled (enabled);
    repaint();
}

void ClipDragComponent::paint (juce::Graphics& g)
{
    const auto enabled = isEnabled();
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    // The clip body is dimmed while dragging is unavailable.
    g.setOpacity (enabled ? 1.0f : 0.45f);
    g.setColour (enabled ? clipBackgroundColour : clipBackgroundColour.darker (0.35f));
    g.fillRoundedRectangle (area, 6.0f);

    g.setColour (enabled ? clipOutlineColour : clipOutlineColour.withAlpha (0.5f));
    g.drawRoundedRectangle (area, 6.0f, 1.0f);

    // A small accent bar on the left makes the component read as a clip.
    const auto accent = area.reduced (6.0f, 8.0f).removeFromLeft (4.0f);
    g.setColour (enabled ? clipAccentColour : clipAccentColour.withAlpha (0.5f));
    g.fillRoundedRectangle (accent, 2.0f);

    auto textArea = getLocalBounds().reduced (18, 6);
    auto titleArea = textArea.removeFromTop (textArea.getHeight() / 2);
    auto detailArea = textArea;

    g.setOpacity (enabled ? 1.0f : 0.6f);
    g.setColour (clipTextColour);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("Drag to DAW", titleArea, juce::Justification::centredLeft, true);

    g.setColour (enabled ? clipDimTextColour : clipErrorTextColour);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    const auto hint = loopBeats > 0.0 ? juce::String ("Fix errors to drag")
                                      : juce::String ("Enter a progression to drag");

    g.drawText (enabled ? describeLoop (loopBeats) : hint,
                detailArea, juce::Justification::centredLeft, true);
}

void ClipDragComponent::mouseDown (const juce::MouseEvent& event)
{
    dragStart = event.getPosition();
    dragStarted = false;
}

void ClipDragComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown() || ! canDrag || dragStarted)
        return;

    // A click must not create a file; wait for a deliberate drag gesture.
    if (dragStart.getDistanceFrom (event.getPosition()) <= 4)
        return;

    dragStarted = true;

    const auto file = createClipFile != nullptr ? createClipFile() : juce::File();

    if (file == juce::File())
        return;

    juce::StringArray files;
    files.add (file.getFullPathName());

    const auto dragQueued = juce::DragAndDropContainer::performExternalDragDropOfFiles (
        files, false, this);

    if (onStatus != nullptr)
    {
        const auto saved = "Clip saved: " + file.getFileName();
        onStatus (dragQueued ? saved + "; drag requested."
                             : saved + "; drag could not be queued.");
    }
}
