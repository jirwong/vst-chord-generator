#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
constexpr int editorWidth = 960;
constexpr int editorHeight = 750;
constexpr int timerFrequencyHz = 30;
constexpr double debounceMilliseconds = 150.0;
constexpr int sequenceCardHeight = 138;
constexpr int sequenceCardGap = 12;
constexpr int visibleSequenceRows = 2;
constexpr int desiredSequenceHeight = visibleSequenceRows * sequenceCardHeight
                                      + (visibleSequenceRows - 1) * sequenceCardGap;
constexpr int minimumCardWidth = 190;

const auto backgroundColour = juce::Colour (0xff121718);
const auto headerColour = juce::Colour (0xff171e20);
const auto panelColour = juce::Colour (0xff1d2527);
const auto outlineColour = juce::Colour (0xff3a4648);
const auto textColour = juce::Colour (0xffe5ebea);
const auto dimTextColour = juce::Colour (0xff9eaaab);
const auto errorColour = juce::Colour (0xffe78480);
const auto accentColour = juce::Colour (0xff70c9c0);
const auto activeCardColour = juce::Colour (0xff203033);
const auto footerColour = juce::Colour (0xff171e20);

void configureLabel (juce::Label& label, const juce::String& text, juce::Colour colour,
                     float fontHeight)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (juce::Font (juce::FontOptions (fontHeight)));
    label.setColour (juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks (false, false);
}

juce::String formatBeats (double beats)
{
    if (std::abs (beats - std::round (beats)) < 1e-9)
        return juce::String (static_cast<int> (std::lround (beats)));

    auto text = juce::String (beats, 3);

    while (text.endsWithChar ('0'))
        text = text.dropLastCharacters (1);

    if (text.endsWithChar ('.'))
        text = text.dropLastCharacters (1);

    return text;
}

juce::String formatCardTiming (const LearningRow& row)
{
    const auto expression = row.lengthExpression.isNotEmpty() ? row.lengthExpression
                                                               : juce::String ("4beats");
    const auto displayExpression = [&]
    {
        for (const auto suffix : {juce::String ("beats"), juce::String ("beat"),
                                  juce::String ("bars"), juce::String ("bar")})
        {
            if (expression.endsWithIgnoreCase (suffix))
            {
                const auto prefix = expression.dropLastCharacters (suffix.length());
                return prefix + " " + suffix.toUpperCase();
            }
        }

        return expression.toUpperCase();
    }();
    const auto beatText = juce::String::fromUTF8 (
        chordtext::formatBeatCount (row.durationBeats).c_str());
    return (row.usesDefaultLength ? juce::String ("DEFAULT  \xc2\xb7 ") : juce::String())
           + displayExpression + juce::String::fromUTF8 ("  \xc2\xb7  ")
           + beatText.toUpperCase();
}

juce::String formatTimingContext (const PublishedTiming& timing)
{
    const auto signature = juce::String (timing.settings.signature.numerator) + "/"
                         + juce::String (timing.settings.signature.denominator);
    return timing.hostSignatureAvailable ? signature + " host" : signature + " fallback";
}

void drawPin (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colours::white.withAlpha (0.82f));
    g.fillEllipse (area.getCentreX() - 3.0f, area.getY() + 1.0f, 6.0f, 6.0f);
    g.drawLine (area.getCentreX(), area.getY() + 6.0f,
                area.getCentreX(), area.getBottom() - 1.0f, 1.3f);
}
} // namespace

ChordSequenceComponent::ChordSequenceComponent()
{
    setComponentID ("chordSequence");
    content.setOwner (this);    setScrollBarsShown (true, false);
    setScrollBarThickness (8);
    getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId,
                                      outlineColour.brighter (0.15f));
    setViewedComponent (&content, false);
}

ChordSequenceComponent::~ChordSequenceComponent()
{
    setViewedComponent (nullptr, false);
}

void ChordSequenceComponent::resized()
{
    auto contentWidth = getWidth();
    auto contentHeight = content.getContentHeightForWidth (contentWidth);
    if (contentHeight > getHeight())
    {
        contentWidth = std::max (0, contentWidth - getScrollBarThickness());
        contentHeight = content.getContentHeightForWidth (contentWidth);
    }

    content.setSize (contentWidth, contentHeight);
}

void ChordSequenceComponent::setRows (std::vector<LearningRow> newRows, int currentStep,
                                      bool stale, std::uint64_t displayedRevision)
{
    content.setRows (std::move (newRows), currentStep, stale, displayedRevision);
    resized();
    repaint();
}

void ChordSequenceComponent::setPreviewFeedback (chordscript::PreviewFeedback feedback)
{
    content.setPreviewFeedback (feedback);
}

juce::Rectangle<int> ChordSequenceComponent::cardBounds (int index) const
{
    return content.cardBounds (index);
}

int ChordSequenceComponent::cardAt (juce::Point<int> point) const
{
    return content.cardAt (point);
}

std::uint64_t ChordSequenceComponent::getDisplayedRevision() const
{
    return content.getDisplayedRevision();
}

void ChordSequenceComponent::CardContent::setRows (std::vector<LearningRow> newRows,
                                                    int newCurrentStep, bool isStale,
                                                    std::uint64_t newDisplayedRevision)
{
    if (newRows.size() == rows.size() && newCurrentStep == currentStep && isStale == stale
        && newDisplayedRevision == displayedRevision)
    {
        auto unchanged = true;
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (rows[index].chord != newRows[index].chord
                || rows[index].degree != newRows[index].degree
                || rows[index].notes != newRows[index].notes
                || rows[index].formula != newRows[index].formula
                || rows[index].bass != newRows[index].bass
                || rows[index].numericDegree != newRows[index].numericDegree
                || rows[index].durationBeats != newRows[index].durationBeats
                || rows[index].lengthExpression != newRows[index].lengthExpression
                || rows[index].usesDefaultLength != newRows[index].usesDefaultLength)
            {
                unchanged = false;
                break;
            }
        }

        if (unchanged)
            return;
    }

    rows = std::move (newRows);
    setTooltip ({});
    currentStep = newCurrentStep;
    stale = isStale;
    displayedRevision = newDisplayedRevision;
    resized();
    repaint();
}

void ChordSequenceComponent::CardContent::setPreviewFeedback (
    chordscript::PreviewFeedback feedback)
{
    if (feedback.mode == previewFeedback.mode
        && feedback.stepIndex == previewFeedback.stepIndex
        && feedback.gestureId == previewFeedback.gestureId
        && feedback.cancellationEpoch == previewFeedback.cancellationEpoch
        && feedback.dataRevision == previewFeedback.dataRevision)
    {
        return;
    }

    previewFeedback = feedback;
    repaint();
}

int ChordSequenceComponent::CardContent::getContentHeightForWidth (int width) const
{
    const auto columns = std::max (1, (width + sequenceCardGap)
                                          / (minimumCardWidth + sequenceCardGap));
    const auto rowCount = rows.empty() ? 1
                                        : (static_cast<int> (rows.size()) + columns - 1) / columns;
    return rowCount * sequenceCardHeight + (rowCount - 1) * sequenceCardGap;
}

void ChordSequenceComponent::CardContent::resized()
{
    const auto availableWidth = getWidth();
    columnCount = std::max (1, (availableWidth + sequenceCardGap)
                                  / (minimumCardWidth + sequenceCardGap));
    cardWidth = (availableWidth - (columnCount - 1) * sequenceCardGap) / columnCount;
}

juce::Rectangle<int> ChordSequenceComponent::CardContent::cardBounds (int index) const
{
    if (index < 0 || static_cast<std::size_t> (index) >= rows.size()
        || cardWidth <= 0 || columnCount <= 0)
    {
        return {};
    }

    const auto column = index % columnCount;
    const auto rowIndex = index / columnCount;
    return juce::Rectangle<int> (column * (cardWidth + sequenceCardGap),
                                 rowIndex * (sequenceCardHeight + sequenceCardGap),
                                 cardWidth, sequenceCardHeight);
}

int ChordSequenceComponent::CardContent::cardAt (juce::Point<int> point) const
{
    for (std::size_t index = 0; index < rows.size(); ++index)
        if (cardBounds (static_cast<int> (index)).contains (point))
            return static_cast<int> (index);

    return -1;
}

void ChordSequenceComponent::CardContent::mouseDown (const juce::MouseEvent& event)
{
    if (stale || rows.empty() || displayedRevision == 0)
        return;

    const auto index = cardAt (event.getPosition());
    if (index < 0)
        return;

    if (event.mods.isPopupMenu())
    {
        if (owner != nullptr && owner->onToggleHeldPreview)
            owner->onToggleHeldPreview (index, displayedRevision);
        return;
    }

    if (event.mods.isLeftButtonDown() && owner != nullptr && owner->onMomentaryPreview)
    {
        const auto gestureId = owner->onMomentaryPreview (index, displayedRevision);
        if (gestureId != 0)
        {
            // Keep press identities in order. A delayed release then ends the
            // gesture that started it instead of a newer replacement.
            if (momentaryGestureIds.size() >= 32)
                momentaryGestureIds.erase (momentaryGestureIds.begin());
            momentaryGestureIds.push_back (gestureId);
        }
    }
}

void ChordSequenceComponent::CardContent::mouseUp (const juce::MouseEvent&)
{
    // JUCE synthesizes a mouse-up on Windows when capture is lost. Releasing
    // the queued identity lets that path end only the gesture that started it.
    if (momentaryGestureIds.empty())
        return;

    const auto gestureId = momentaryGestureIds.front();
    momentaryGestureIds.erase (momentaryGestureIds.begin());

    if (owner != nullptr && owner->onMomentaryRelease)
        owner->onMomentaryRelease (gestureId);
}

void ChordSequenceComponent::CardContent::mouseMove (const juce::MouseEvent& event)
{
    const auto index = cardAt (event.getPosition());
    if (index < 0)
    {
        setTooltip ({});
        return;
    }

    const auto& row = rows[static_cast<std::size_t> (index)];
    auto text = row.chord + "\n";
    text += row.formula.isNotEmpty() ? row.formula : row.degree;
    text += "\n" + row.notes;
    if (row.bass.isNotEmpty())
        text += "\nBass: " + row.bass;
    text += "\nLength: " + (row.lengthExpression.isNotEmpty() ? row.lengthExpression
                                                                 : juce::String ("4beats"));
    text += " (" + juce::String::fromUTF8 (
        chordtext::formatBeatCount (row.durationBeats).c_str()) + ")";
    if (row.usesDefaultLength)
        text += "\nDEFAULT";
    setTooltip (text);
}

void ChordSequenceComponent::CardContent::mouseExit (const juce::MouseEvent&)
{
    setTooltip ({});
}

void ChordSequenceComponent::CardContent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    if (rows.empty())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (stale ? "Fix the progression above to build the sequence"
                          : "Enter numbers, chord symbols, or Roman numerals to preview the sequence",
                    getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    g.beginTransparencyLayer (stale ? 0.46f : 1.0f);
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        auto card = cardBounds (static_cast<int> (index)).toFloat();
        const auto isManual = previewFeedback.mode != chordscript::PreviewMode::idle
                              && static_cast<int> (index) == previewFeedback.stepIndex;
        const auto isCurrent = ! stale
                               && previewFeedback.mode == chordscript::PreviewMode::idle
                               && static_cast<int> (index) == currentStep;
        const auto isHighlighted = isManual || isCurrent;

        g.setColour (isHighlighted ? activeCardColour : panelColour);
        g.fillRoundedRectangle (card, 9.0f);
        g.setColour (isHighlighted ? accentColour : outlineColour);
        g.drawRoundedRectangle (card, 9.0f, isHighlighted ? 1.4f : 1.0f);

        if (isHighlighted)
        {
            g.setColour (accentColour);
            g.fillRoundedRectangle (card.withHeight (4.0f).reduced (9.0f, 0.0f), 2.0f);
        }

        auto inner = card.toNearestInt().reduced (16, 12);
        auto titleRow = inner.removeFromTop (27);
        const auto timingRow = inner.removeFromBottom (16);

        if (isManual)
        {
            const auto badge = titleRow.removeFromRight (60)
                                         .withSizeKeepingCentre (58, 16).toFloat();
            g.setColour (accentColour.withAlpha (0.16f));
            g.fillRoundedRectangle (badge, 4.0f);

            auto badgeContent = badge.reduced (6.0f, 2.0f);
            if (previewFeedback.mode == chordscript::PreviewMode::held)
            {
                drawPin (g, badgeContent.removeFromLeft (10).reduced (1.0f, 1.0f));
                badgeContent.removeFromLeft (3.0f);
            }

            g.setColour (accentColour);
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawFittedText (previewFeedback.mode == chordscript::PreviewMode::held ? "HELD"
                                                                                     : "PREVIEW",
                              badgeContent.toNearestInt(), juce::Justification::centred, 1);
        }
        else if (isCurrent)
        {
            const auto badge = titleRow.removeFromRight (60)
                                        .withSizeKeepingCentre (58, 16).toFloat();
            g.setColour (accentColour);
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText ("PLAYING", badge, juce::Justification::centredRight, false);
        }

        g.setColour (textColour);
        g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawFittedText (rows[index].chord, titleRow,
                          juce::Justification::centredLeft, 1);
        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const auto analysis = rows[index].formula.isNotEmpty() ? rows[index].formula
            : rows[index].numericDegree > 0
                ? "Degree " + juce::String (rows[index].numericDegree)
                    + juce::String::fromUTF8 (" \xc2\xb7 ") + rows[index].degree
                : rows[index].degree;
        g.drawText (analysis, inner.removeFromTop (18), juce::Justification::centredLeft, true);

        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (rows[index].notes, inner.removeFromTop (
                        rows[index].bass.isNotEmpty() ? 24 : 35),
                    juce::Justification::centredLeft, true);

        if (rows[index].bass.isNotEmpty())
        {
            g.setColour (accentColour);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("Bass: " + rows[index].bass, inner.removeFromTop (18),
                              juce::Justification::centredLeft, 1);
        }

        g.setColour (rows[index].usesDefaultLength ? accentColour : dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (10.0f,
                                                   rows[index].usesDefaultLength
                                                       ? juce::Font::bold
                                                       : juce::Font::plain)));
        g.drawFittedText (formatCardTiming (rows[index]), timingRow,
                          juce::Justification::bottomLeft, 1);
    }

    g.endTransparencyLayer();
}

ChordTextAudioProcessorEditor::ChordTextAudioProcessorEditor (ChordTextAudioProcessor& p, UiSettings settings)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      uiSettings (std::move (settings)),
      tooltipWindow (this)
{
    editorContent.setComponentID ("editorContent");
    addAndMakeVisible (editorContent);
    setSize (editorWidth, editorHeight);
    setWantsKeyboardFocus (true);

    configureLabel (brandLabel, "CHORDSCRIPT", textColour, 14.0f);
    brandLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    configureLabel (productLabel, "MIDI GENERATOR", dimTextColour, 11.0f);
    configureLabel (transportStatusLabel, {}, dimTextColour, 11.0f);
    configureLabel (progressionLabel, "PROGRESSION", dimTextColour, 11.0f);
    configureLabel (progressionHintLabel, "Type numbers, chord symbols, or Roman numerals",
                    dimTextColour, 11.0f);
    configureLabel (keyLabel, "KEY", dimTextColour, 11.0f);
    configureLabel (modeLabel, "MODE", dimTextColour, 11.0f);
    configureLabel (scaleSummaryLabel, {}, dimTextColour, 12.0f);
    scaleSummaryLabel.setComponentID ("scaleSummary");
    configureLabel (defaultLengthLabel, "DEFAULT LENGTH", dimTextColour, 11.0f);
    defaultLengthLabel.setComponentID ("defaultLengthLabel");
    configureLabel (bassLabel, "BASS", dimTextColour, 11.0f);
    bassLabel.setComponentID ("bassLabel");
    configureLabel (voicingLabel, "VOICING", dimTextColour, 11.0f);
    configureLabel (rangeLabel, "RANGE", dimTextColour, 11.0f);
    configureLabel (velocityLabel, "VELOCITY", dimTextColour, 11.0f);
    configureLabel (sequenceLabel, "CHORD SEQUENCE", dimTextColour, 11.0f);
    configureLabel (sequenceMetaLabel, {}, dimTextColour, 11.0f);
    configureLabel (previewHintLabel,
                    juce::String::fromUTF8 ("Left press to preview \xc2\xb7 Right-click to hold"),
                    dimTextColour, 10.0f);
    configureLabel (sequenceStatusLabel, {}, accentColour, 11.0f);
    configureLabel (statusLabel, {}, dimTextColour, 11.0f);
    configureLabel (uiScaleLabel, "UI SCALE", dimTextColour, 11.0f);
    uiScaleLabel.setComponentID ("uiScaleLabel");
    uiScaleBox.setComponentID ("uiScaleBox");
    for (const auto percent : {75, 100, 125, 150})
        uiScaleBox.addItem (juce::String (percent) + "%", percent);
    uiScaleBox.setColour (juce::ComboBox::backgroundColourId, panelColour);
    uiScaleBox.setColour (juce::ComboBox::textColourId, textColour);
    uiScaleBox.setColour (juce::ComboBox::outlineColourId, outlineColour);
    uiScaleBox.setColour (juce::ComboBox::arrowColourId, dimTextColour);
    editorContent.addAndMakeVisible (uiScaleLabel);
    editorContent.addAndMakeVisible (uiScaleBox);
    uiScaleBox.onChange = [this]
    {
        const auto percent = uiScaleBox.getSelectedId();
        if (! UiSettings::isSupportedScale (percent) || percent == appliedUiScalePercent)
            return;
        applyUiScale (percent);
        uiScaleSaveError = uiSettings.saveUiScalePercent (percent).failed()
            ? juce::String ("UI scale was not saved") : juce::String();
        refreshStatus();
    };

    progressionEditor.setMultiLine (true, true);
    progressionEditor.setReturnKeyStartsNewLine (true);
    progressionEditor.setScrollbarsShown (true);
    progressionEditor.setComponentID ("progressionEditor");
    progressionEditor.setText (processor.getProgressionText(), juce::dontSendNotification);
    progressionEditor.setColour (juce::TextEditor::backgroundColourId, panelColour);
    progressionEditor.setColour (juce::TextEditor::textColourId, textColour);
    progressionEditor.setColour (juce::TextEditor::outlineColourId, outlineColour);
    progressionEditor.setColour (juce::TextEditor::focusedOutlineColourId,
                                 accentColour);
    progressionEditor.setFont (juce::Font (juce::FontOptions (14.0f)));
    progressionEditor.onTextChange = [this]
    {
        textEditPending = true;
        processor.setPreviewInputPending (true);
        lastTextEditMs = juce::Time::getMillisecondCounterHiRes();
        refreshStatus();
    };

    // Read-only, scrollable area: every diagnostic is shown, not just the
    // first two.
    diagnosticsDisplay.setMultiLine (true, true);
    diagnosticsDisplay.setComponentID ("diagnosticsDisplay");
    diagnosticsDisplay.setScrollbarsShown (true);
    diagnosticsDisplay.setReadOnly (true);
    diagnosticsDisplay.setCaretVisible (false);
    diagnosticsDisplay.setPopupMenuEnabled (false);
    diagnosticsDisplay.setColour (juce::TextEditor::backgroundColourId, panelColour);
    diagnosticsDisplay.setColour (juce::TextEditor::textColourId, errorColour);
    diagnosticsDisplay.setColour (juce::TextEditor::outlineColourId, outlineColour);
    diagnosticsDisplay.setColour (juce::TextEditor::focusedOutlineColourId, outlineColour);
    diagnosticsDisplay.setFont (juce::Font (juce::FontOptions (12.0f)));

    auto styleAndAddCombo = [this] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, panelColour);
        box.setColour (juce::ComboBox::textColourId, textColour);
        box.setColour (juce::ComboBox::outlineColourId, outlineColour);
        box.setColour (juce::ComboBox::arrowColourId, dimTextColour);
        editorContent.addAndMakeVisible (box);
    };

    auto setUpCombo = [&] (juce::ComboBox& box, const char* parameterId)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (parameterId)))
            box.addItemList (choice->choices, 1);

        styleAndAddCombo (box);
    };

    setUpCombo (keyBox, "key");
    setUpCombo (modeBox, "scaleMode");
    modeBox.setComponentID ("modeBox");
    setUpCombo (voicingBox, "voicing");
    setUpCombo (rangeBox, "range");
    defaultLengthBox.setComponentID ("defaultLengthBox");
    defaultLengthBox.addSectionHeading ("NOTES");
    for (int index = 0; index <= 12; ++index)
    {
        const auto& preset = chordtext::lengthPresets()[static_cast<std::size_t> (index)];
        defaultLengthBox.addItem (juce::String::fromUTF8 (preset.displayName.data(),
                                                           static_cast<int> (preset.displayName.size())),
                                  index + 1);
    }
    defaultLengthBox.addSectionHeading ("BEATS");
    for (int index = 13; index <= 15; ++index)
    {
        const auto& preset = chordtext::lengthPresets()[static_cast<std::size_t> (index)];
        defaultLengthBox.addItem (juce::String::fromUTF8 (preset.displayName.data(),
                                                           static_cast<int> (preset.displayName.size())),
                                  index + 1);
    }
    defaultLengthBox.addSectionHeading ("BARS");
    for (int index = 16; index <= 18; ++index)
    {
        const auto& preset = chordtext::lengthPresets()[static_cast<std::size_t> (index)];
        defaultLengthBox.addItem (juce::String::fromUTF8 (preset.displayName.data(),
                                                           static_cast<int> (preset.displayName.size())),
                                  index + 1);
    }
    styleAndAddCombo (defaultLengthBox);
    setUpCombo (bassBox, "bassOctaves");
    bassBox.setComponentID ("bassBox");

    velocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 22);
    velocitySlider.setColour (juce::Slider::textBoxTextColourId, textColour);
    velocitySlider.setColour (juce::Slider::textBoxBackgroundColourId, panelColour);
    velocitySlider.setColour (juce::Slider::textBoxOutlineColourId, outlineColour);
    velocitySlider.setColour (juce::Slider::trackColourId, accentColour);
    velocitySlider.setColour (juce::Slider::backgroundColourId, outlineColour);
    velocitySlider.setColour (juce::Slider::thumbColourId, accentColour);

    loopButton.setColour (juce::ToggleButton::textColourId, textColour);
    loopButton.setColour (juce::ToggleButton::tickColourId, accentColour);
    loopButton.setComponentID ("loopButton");

    auto styleButton = [] (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, panelColour);
        button.setColour (juce::TextButton::buttonOnColourId, activeCardColour);
        button.setColour (juce::TextButton::textColourOffId, textColour);
        button.setColour (juce::TextButton::textColourOnId, accentColour);
    };
    styleButton (exportButton);
    styleButton (regenerateButton);
    styleButton (revealClipButton);

    exportButton.onClick = [this] { startExport(); };
    exportButton.setComponentID ("exportButton");
    exportButton.setEnabled (false);

    regenerateButton.setComponentID ("regenerateButton");
    regenerateButton.onClick = [this]
    {
        applyProgressionText (true);
        refreshStatus();
    };

    revealClipButton.setComponentID ("revealFile");
    revealClipButton.setEnabled (false);
    revealClipButton.onClick = [this]
    {
        if (latestOutputFile.existsAsFile())
            latestOutputFile.revealToUser();
    };

    clipDrag.setComponentID ("clipDrag");
    clipDrag.createClipFile = [this]
    {
        // Apply a pending edit and any pending musical-parameter rebuild, but
        // leave a coherent unchanged plan alone so a drag cannot cancel a hold.
        applyProgressionText (processor.isPlanRebuildPending());

        if (! processor.getDiagnostics().empty())
        {
            lastExportStatus = "Fix the progression errors before dragging.";
            refreshStatus();
            return juce::File();
        }

        juce::String error;
        const auto file = processor.createDragClipFile (error);

        if (file == juce::File())
        {
            lastExportStatus = "Drag failed: " + error;
            refreshStatus();
        }

        return file;
    };
    clipDrag.onStatus = [this] (const juce::String& status)
    {
        lastExportStatus = status;
        refreshStatus();
    };

    sequenceStatusLabel.setComponentID ("sequenceStatus");
    sequenceLabel.setComponentID ("sequenceLabel");
    sequenceMetaLabel.setComponentID ("sequenceMeta");
    previewHintLabel.setComponentID ("previewHint");
    previewHintLabel.setInterceptsMouseClicks (false, false);
    sequenceView.setComponentID ("chordSequence");
    sequenceView.onMomentaryPreview = [this] (int stepIndex, std::uint64_t displayedRevision)
    {
        return processor.beginMomentaryPreview (stepIndex, displayedRevision);
    };
    sequenceView.onMomentaryRelease = [this] (chordscript::PreviewGestureId gestureId)
    {
        processor.endMomentaryPreview (gestureId);
    };
    sequenceView.onToggleHeldPreview = [this] (int stepIndex, std::uint64_t displayedRevision)
    {
        processor.toggleHeldPreview (stepIndex, displayedRevision);
    };
    brandLabel.setComponentID ("brandLabel");
    transportStatusLabel.setComponentID ("transportStatus");
    statusLabel.setComponentID ("footerStatus");

    editorContent.addAndMakeVisible (brandLabel);
    editorContent.addAndMakeVisible (productLabel);
    editorContent.addAndMakeVisible (transportStatusLabel);
    editorContent.addAndMakeVisible (progressionLabel);
    editorContent.addAndMakeVisible (progressionHintLabel);
    editorContent.addAndMakeVisible (progressionEditor);
    editorContent.addAndMakeVisible (keyLabel);
    editorContent.addAndMakeVisible (modeLabel);
    editorContent.addAndMakeVisible (scaleSummaryLabel);
    editorContent.addAndMakeVisible (voicingLabel);
    editorContent.addAndMakeVisible (rangeLabel);
    editorContent.addAndMakeVisible (velocityLabel);
    editorContent.addAndMakeVisible (velocitySlider);
    editorContent.addAndMakeVisible (defaultLengthLabel);
    editorContent.addAndMakeVisible (bassLabel);
    editorContent.addAndMakeVisible (loopButton);
    editorContent.addAndMakeVisible (exportButton);
    editorContent.addAndMakeVisible (regenerateButton);
    editorContent.addAndMakeVisible (revealClipButton);
    editorContent.addAndMakeVisible (clipDrag);
    editorContent.addAndMakeVisible (sequenceLabel);
    editorContent.addAndMakeVisible (sequenceMetaLabel);
    editorContent.addAndMakeVisible (previewHintLabel);
    editorContent.addAndMakeVisible (sequenceStatusLabel);
    editorContent.addAndMakeVisible (statusLabel);
    editorContent.addAndMakeVisible (diagnosticsDisplay);
    editorContent.addAndMakeVisible (sequenceView);

    keyAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "key", keyBox);
    modeAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "scaleMode", modeBox);
    voicingAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "voicing",
                                                             voicingBox);
    rangeAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "range", rangeBox);
    defaultLengthAttachment = std::make_unique<ComboBoxAttachment> (
        processor.apvts, "defaultLength", defaultLengthBox);
    bassAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "bassOctaves",
                                                           bassBox);
    velocityAttachment = std::make_unique<SliderAttachment> (processor.apvts, "velocity",
                                                             velocitySlider);
    loopAttachment = std::make_unique<ButtonAttachment> (processor.apvts, "loop", loopButton);

    const auto initialScale = uiSettings.loadUiScalePercent();
    uiScaleBox.setSelectedId (initialScale, juce::dontSendNotification);
    applyUiScale (initialScale);
    refreshStatus();
    startTimerHz (timerFrequencyHz);
}

ChordTextAudioProcessorEditor::~ChordTextAudioProcessorEditor()
{
    stopTimer();

    // Cancel the preview before flushing a pending debounced edit.
    processor.cancelChordPreview();
    processor.setPreviewInputPending (false);

    // Do not lose a pending debounced edit when the editor closes.
    if (textEditPending)
        applyProgressionText (false);

    fileChooser = nullptr;
}

void ChordTextAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
}

void ChordTextAudioProcessorEditor::ContentComponent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    auto area = getLocalBounds();
    const auto header = area.removeFromTop (58);
    const auto footer = area.removeFromBottom (48);
    g.setColour (headerColour);
    g.fillRect (header);
    g.setColour (footerColour);
    g.fillRect (footer);
    g.setColour (outlineColour.withAlpha (0.7f));
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));
    g.drawHorizontalLine (footer.getY(), 0.0f, static_cast<float> (getWidth()));
}

void ChordTextAudioProcessorEditor::resized()
{
    editorContent.setBounds (0, 0, editorWidth, editorHeight);
    layoutContent();
}

void ChordTextAudioProcessorEditor::applyUiScale (int percent)
{
    if (! UiSettings::isSupportedScale (percent))
        return;
    appliedUiScalePercent = percent;
    editorContent.setTransform (juce::AffineTransform::scale (static_cast<float> (percent) / 100.0f));
    setSize (static_cast<int> (std::lround (editorWidth * percent / 100.0)),
             static_cast<int> (std::lround (editorHeight * percent / 100.0)));
    layoutContent();
    repaint();
}

void ChordTextAudioProcessorEditor::layoutContent()
{
    auto area = editorContent.getLocalBounds();
    const auto header = area.removeFromTop (58);
    const auto footer = area.removeFromBottom (48);

    brandLabel.setBounds (28, header.getY() + 15, 112, 24);
    productLabel.setBounds (145, header.getY() + 17, 140, 20);
    transportStatusLabel.setBounds (editorWidth - 270, header.getY() + 16, 242, 22);
    transportStatusLabel.setJustificationType (juce::Justification::centredRight);
    auto footerArea = footer.reduced (28, 0);
    uiScaleBox.setBounds (footerArea.removeFromRight (86).withSizeKeepingCentre (86, 28));
    footerArea.removeFromRight (10);
    uiScaleLabel.setBounds (footerArea.removeFromRight (65));
    footerArea.removeFromRight (12);
    statusLabel.setBounds (footerArea);

    auto content = area.reduced (28, 10);
    auto progressionHeader = content.removeFromTop (32);
    regenerateButton.setBounds (progressionHeader.removeFromRight (100).withHeight (30));
    progressionHeader.removeFromRight (10);
    progressionLabel.setBounds (progressionHeader.removeFromTop (15));
    progressionHintLabel.setBounds (progressionHeader);

    progressionEditor.setBounds (content.removeFromTop (86));
    const auto diagnostics = processor.getDiagnostics();
    const auto diagnosticLineCount = static_cast<int> (diagnostics.size());
    const auto diagnosticsHeight = diagnosticsDisplay.isVisible()
                                       ? std::clamp (diagnosticLineCount * 18 + 6, 24, 60)
                                       : 0;
    if (diagnosticsHeight > 0)
        content.removeFromTop (4);
    diagnosticsDisplay.setBounds (content.removeFromTop (diagnosticsHeight));
    content.removeFromTop (diagnosticsHeight > 0 ? 8 : 12);

    const auto controlLabels = content.removeFromTop (16);
    const auto controlFields = content.removeFromTop (40);
    content.removeFromTop (14);

    constexpr int gap = 12;
    int x = controlFields.getX();
    auto placeControl = [&] (juce::Label& label, juce::Component& field, int width)
    {
        label.setBounds (x, controlLabels.getY(), width, controlLabels.getHeight());
        field.setBounds (x, controlFields.getY(), width, controlFields.getHeight());
        x += width + gap;
    };

    placeControl (keyLabel, keyBox, 120);
    placeControl (modeLabel, modeBox, 185);
    placeControl (voicingLabel, voicingBox, 160);
    placeControl (rangeLabel, rangeBox, 115);
    const auto loopWidth = 60;
    const auto velocityWidth = std::max (80, controlFields.getRight() - x - loopWidth - gap);
    placeControl (velocityLabel, velocitySlider, velocityWidth);
    loopButton.setBounds (x, controlFields.getY(), loopWidth, controlFields.getHeight());
    const auto summaryRow = content.removeFromTop (28);
    constexpr int bassControlWidth = 224;
    constexpr int defaultLengthControlWidth = 212;
    constexpr int summaryGap = 12;
    const auto bassControlX = summaryRow.getRight() - bassControlWidth;
    const auto defaultLengthControlX = bassControlX - summaryGap - defaultLengthControlWidth;
    scaleSummaryLabel.setBounds (summaryRow.withTrimmedRight (
        bassControlWidth + defaultLengthControlWidth + summaryGap * 2));
    defaultLengthLabel.setBounds (defaultLengthControlX, summaryRow.getY() + 6, 112, 16);
    defaultLengthBox.setBounds (defaultLengthControlX + 112, summaryRow.getY(),
                                defaultLengthControlWidth - 112, summaryRow.getHeight());
    bassLabel.setBounds (bassControlX, summaryRow.getY() + 6, 44, 16);
    bassBox.setBounds (bassControlX + 52, summaryRow.getY(), 172, summaryRow.getHeight());
    content.removeFromTop (4);

    constexpr int sequenceHeaderHeight = 40;
    constexpr int actionRowHeight = 44;
    constexpr int actionGap = 8;
    const auto sequenceHeight = std::min (desiredSequenceHeight,
        std::max (1, content.getHeight() - sequenceHeaderHeight - actionRowHeight - actionGap));
    const auto flexibleSequenceGap = std::max (0, content.getHeight() - sequenceHeaderHeight
        - sequenceHeight - actionRowHeight - actionGap);
    content.removeFromTop (flexibleSequenceGap);

    auto sequenceHeader = content.removeFromTop (sequenceHeaderHeight);
    const auto sequenceHeaderY = sequenceHeader.getY();
    sequenceLabel.setBounds (sequenceHeader.removeFromTop (17));
    sequenceStatusLabel.setBounds (editorWidth - 228, sequenceHeaderY + 1, 200, 23);
    sequenceMetaLabel.setBounds (sequenceHeader.removeFromLeft (170));
    previewHintLabel.setBounds (sequenceHeader.removeFromLeft (330).reduced (0, 2));

    sequenceView.setBounds (content.removeFromTop (sequenceHeight));
    content.removeFromTop (actionGap);

    auto actionRow = content.removeFromBottom (actionRowHeight);
    revealClipButton.setBounds (actionRow.removeFromRight (156));
    actionRow.removeFromRight (10);
    exportButton.setBounds (actionRow.removeFromRight (148));
    actionRow.removeFromRight (10);
    clipDrag.setBounds (actionRow);
}

void ChordTextAudioProcessorEditor::timerCallback()
{
    if (textEditPending
        && juce::Time::getMillisecondCounterHiRes() - lastTextEditMs >= debounceMilliseconds)
        applyProgressionText (false);

    refreshStatus();
}

void ChordTextAudioProcessorEditor::applyProgressionText (bool force)
{
    textEditPending = false;
    const auto text = progressionEditor.getText();

    if (force || text != processor.getProgressionText())
        processor.setProgressionText (text);

    processor.setPreviewInputPending (false);
}

void ChordTextAudioProcessorEditor::startExport()
{
    if (fileChooser != nullptr || ! exportButton.isEnabled())
        return;

    const auto initialFile = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile ("chord-text.mid");

    exportButton.setEnabled (false);
    fileChooser = std::make_unique<juce::FileChooser> ("Export MIDI file", initialFile, "*.mid");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting;

    juce::Component::SafePointer<ChordTextAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser) mutable
    {
        if (safeThis == nullptr)
            return;

        const auto chosen = chooser.getResult();

        if (chosen != juce::File())
        {
            const auto file = chosen.hasFileExtension ("mid") ? chosen
                                                              : chosen.withFileExtension (".mid");
            juce::String error;

            if (safeThis->processor.exportMidiFile (file, error))
            {
                safeThis->latestOutputFile = file;
                safeThis->lastRevealAvailable = true;
                safeThis->lastExportStatus = "Exported " + file.getFileName();
            }
            else
                safeThis->lastExportStatus = "Export failed: " + error;
        }

        safeThis->fileChooser.reset();
        safeThis->refreshStatus();
    });
}

void ChordTextAudioProcessorEditor::refreshStatus()
{
    const auto scaleSummary = processor.getScaleSummary();
    if (scaleSummaryLabel.getText() != scaleSummary)
        scaleSummaryLabel.setText (scaleSummary, juce::dontSendNotification);
    const auto diagnostics = processor.getDiagnostics();
    const auto rows = processor.getLearningRows();
    const auto timing = processor.getPublishedTiming();
    const auto timingPending = processor.isPlanRebuildPending();
    const auto shouldShowDiagnostics = ! textEditPending && ! diagnostics.empty();
    auto layoutChanged = diagnosticsDisplay.isVisible() != shouldShowDiagnostics
                         || lastTextEditPending != textEditPending;

    const auto statusText = buildStatusText();

    if (statusText != lastStatusText)
    {
        lastStatusText = statusText;
        statusLabel.setText (statusText, juce::dontSendNotification);
    }

    juce::String diagnosticsText;

    for (std::size_t index = 0; index < diagnostics.size(); ++index)
    {
        if (index > 0)
            diagnosticsText += "\n";

        diagnosticsText += describeDiagnostic (diagnostics[index]);
    }

    if (diagnosticsText != lastDiagnosticsText)
    {
        lastDiagnosticsText = diagnosticsText;
        diagnosticsDisplay.setText (diagnosticsText, juce::dontSendNotification);
        layoutChanged = true;
    }

    if (diagnosticsDisplay.isVisible() != shouldShowDiagnostics)
        diagnosticsDisplay.setVisible (shouldShowDiagnostics);

    const auto previewFeedback = processor.getPreviewFeedback();
    const auto previewCardRevision = processor.getPreviewCardRevision();
    const auto previewChord = previewFeedback.stepIndex >= 0
                                  && static_cast<std::size_t> (previewFeedback.stepIndex) < rows.size()
                              ? rows[static_cast<std::size_t> (previewFeedback.stepIndex)].chord
                              : juce::String();

    const auto sequenceStatus = textEditPending ? juce::String ("Updating preview")
                               : timingPending ? juce::String ("Timing updating")
                               : ! diagnostics.empty()
                                   ? rows.empty() ? juce::String ("Fix progression errors")
                                                  : juce::String ("Last valid preview")
                               : rows.empty() ? juce::String ("Enter a progression")
                               : previewFeedback.mode == chordscript::PreviewMode::held
                                   ? previewChord + juce::String::fromUTF8 (" held \xc2\xb7 Right-click again to release")
                               : previewFeedback.mode == chordscript::PreviewMode::momentary
                                   ? previewChord + juce::String::fromUTF8 (" preview \xc2\xb7 Release to stop")
                               : juce::String::fromUTF8 ("\xe2\x9c\x93  Valid progression");
    if (sequenceStatusLabel.getText() != sequenceStatus)
        sequenceStatusLabel.setText (sequenceStatus, juce::dontSendNotification);
    const auto sequenceStatusColour = textEditPending || timingPending
                                           ? juce::Colour (0xffd5aa61)
                                       : ! diagnostics.empty() ? errorColour
                                       : rows.empty() ? dimTextColour : accentColour;
    if (sequenceStatusLabel.findColour (juce::Label::textColourId) != sequenceStatusColour)
        sequenceStatusLabel.setColour (juce::Label::textColourId, sequenceStatusColour);

    double totalBeats = 0.0;

    for (const auto& row : rows)
        totalBeats += row.durationBeats;

    const auto timingText = timingPending ? juce::String ("Timing updating")
                                          : formatTimingContext (timing);
    const auto sequenceMeta = rows.empty() ? juce::String ("No chords yet  \xc2\xb7  ") + timingText
                               : juce::String (static_cast<int> (rows.size())) + " chords  \xc2\xb7  "
                                     + formatBeats (totalBeats) + " beats  \xc2\xb7  " + timingText;
    if (sequenceMetaLabel.getText() != sequenceMeta)
        sequenceMetaLabel.setText (sequenceMeta, juce::dontSendNotification);
    sequenceView.setRows (rows, processor.getCurrentStep(),
                          textEditPending || timingPending || ! diagnostics.empty(), previewCardRevision);
    sequenceView.setPreviewFeedback (previewFeedback);
    refreshTransportStatus();

    const bool canDrag = ! textEditPending && ! timingPending && diagnostics.empty() && ! rows.empty();
    clipDrag.setLoopInfo (totalBeats, canDrag, timing.settings.signature);

    // Check the last drag file only when its path changes instead of doing a
    // file-system stat on every 30 Hz timer tick. Reveal is session-only; an
    // external deletion of the file is an accepted edge case.
    const auto lastDragFile = processor.getLastDragClipFile();
    const auto lastDragPath = lastDragFile.getFullPathName();

    if (lastDragPath != lastRevealPath)
    {
        lastRevealPath = lastDragPath;
        if (lastDragPath.isNotEmpty())
        {
            latestOutputFile = lastDragFile;
            lastRevealAvailable = lastDragFile.existsAsFile();
        }
    }

    revealClipButton.setEnabled (lastRevealAvailable);

    const auto canExport = fileChooser == nullptr && canDrag;

    if (exportButton.isEnabled() != canExport)
        exportButton.setEnabled (canExport);

    if (layoutChanged)
        layoutContent();
    lastTextEditPending = textEditPending;
}

juce::String ChordTextAudioProcessorEditor::buildStatusText() const
{
    const auto rows = processor.getLearningRows();
    const auto diagnostics = processor.getDiagnostics();
    juce::String text = textEditPending ? juce::String ("Updating preview")
                       : ! diagnostics.empty() ? juce::String ("Fix errors to prepare MIDI")
                       : rows.empty() ? juce::String ("No progression")
                                      : juce::String (static_cast<int> (rows.size()))
                                            + juce::String::fromUTF8 (" chords \xc2\xb7 MIDI ready");

    if (uiScaleSaveError.isNotEmpty())
        text = uiScaleSaveError + juce::String::fromUTF8 (" \xc2\xb7 ") + text;

    if (lastExportStatus.isNotEmpty())
        text += juce::String::fromUTF8 (" \xc2\xb7 ") + lastExportStatus;

    return text;
}

void ChordTextAudioProcessorEditor::refreshTransportStatus()
{
    const auto isPlaying = processor.isTransportAvailable();
    const auto hasPlayhead = processor.isPlayheadAvailable();
    const auto text = isPlaying ? juce::String ("Transport playing")
                    : hasPlayhead ? juce::String ("Transport stopped")
                                  : juce::String ("No transport available");
    const auto colour = isPlaying ? accentColour
                       : hasPlayhead ? juce::Colour (0xffd5aa61)
                                     : dimTextColour;

    const auto decoratedText = juce::String::fromUTF8 ("\xe2\x97\x8f  ") + text;
    if (transportStatusLabel.getText() != decoratedText)
        transportStatusLabel.setText (decoratedText, juce::dontSendNotification);
    if (transportStatusLabel.findColour (juce::Label::textColourId) != colour)
        transportStatusLabel.setColour (juce::Label::textColourId, colour);
}

juce::String ChordTextAudioProcessorEditor::describeDiagnostic (
    const chordtext::Diagnostic& diagnostic) const
{
    juce::String text = "Error at character ";
    text += juce::String (static_cast<int> (diagnostic.offset));
    text += ": ";
    text += juce::String (diagnostic.message);

    if (! diagnostic.token.empty())
    {
        text += " (";
        text += juce::String (diagnostic.token);
        text += ")";
    }

    return text;
}
