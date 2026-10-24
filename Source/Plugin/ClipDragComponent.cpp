#include "ClipDragComponent.h"

#include <cmath>

namespace
{
const auto clipBackgroundColour = juce::Colour (0xff203033);
const auto clipOutlineColour = juce::Colour (0xff52777a);
const auto clipAccentColour = juce::Colour (0xff70c9c0);
const auto clipTextColour = juce::Colour (0xffe5ebea);
const auto clipDimTextColour = juce::Colour (0xffa9b7b7);
const auto clipErrorTextColour = juce::Colour (0xffe78480);

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

juce::String describeLoop (double beats, chordtext::TimeSignature signature)
{
    if (beats <= 0.0)
        return "No loop";

    if (! chordtext::isValidTimeSignature (signature))
        signature = {};

    const auto beatsPerBar = static_cast<double> (signature.numerator) * 4.0
                           / static_cast<double> (signature.denominator);
    const auto bars = beats / beatsPerBar;
    return formatNumber (beats) + " beats \xc2\xb7 " + formatNumber (bars)
           + (std::abs (bars - 1.0) < 1.0e-9 ? " bar (" : " bars (")
           + juce::String (signature.numerator) + "/" + juce::String (signature.denominator) + ")";
}
} // namespace

void ClipDragComponent::setLoopInfo (double beats, bool enabled,
                                     chordtext::TimeSignature signature)
{
    if (loopBeats == beats && loopSignature == signature && canDrag == enabled
        && isEnabled() == enabled)
        return;

    loopBeats = beats;
    loopSignature = signature;
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

    // Draw a tiny monochrome keyboard so the action reads as MIDI at a glance.
    auto keyboard = getLocalBounds().withSizeKeepingCentre (22, 14).translated (-getWidth() / 2 + 26, 0).toFloat();
    g.setOpacity (enabled ? 1.0f : 0.55f);
    g.setColour (clipTextColour);
    g.drawRoundedRectangle (keyboard, 1.0f, 1.0f);

    const auto whiteKeyWidth = keyboard.getWidth() / 7.0f;
    for (int key = 1; key < 7; ++key)
        g.drawVerticalLine (static_cast<int> (keyboard.getX() + whiteKeyWidth * key),
                            keyboard.getY() + 1.0f, keyboard.getBottom() - 1.0f);

    for (const auto key : { 0, 1, 3, 4, 5 })
    {
        auto blackKey = juce::Rectangle<float> (keyboard.getX() + whiteKeyWidth * (key + 1) - 2.0f,
                                                 keyboard.getY(), 4.0f, keyboard.getHeight() * 0.57f);
        g.fillRect (blackKey);
    }

    auto textArea = getLocalBounds().withTrimmedLeft (42).withTrimmedRight (10);
    auto titleArea = textArea.removeFromTop (textArea.getHeight() / 2);
    auto detailArea = textArea;

    g.setOpacity (enabled ? 1.0f : 0.6f);
    g.setColour (clipTextColour);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("DRAG CLIP TO DAW", titleArea, juce::Justification::centredLeft, true);

    g.setColour (enabled ? clipDimTextColour : clipErrorTextColour);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    const auto hint = loopBeats > 0.0 ? juce::String ("Fix errors to drag")
                                      : juce::String ("Enter a progression to drag");

    g.drawText (enabled ? describeLoop (loopBeats, loopSignature) : hint,
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
