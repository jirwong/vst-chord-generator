#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
constexpr int editorWidth = 960;
constexpr int editorHeight = 650;
constexpr int timerFrequencyHz = 30;
constexpr double debounceMilliseconds = 150.0;
constexpr int sequenceCardHeight = 138;
constexpr int sequenceCardGap = 12;
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
} // namespace

ChordSequenceComponent::ChordSequenceComponent()
{
    setComponentID ("chordSequence");
    setScrollBarsShown (true, false);
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
                                      bool stale)
{
    content.setRows (std::move (newRows), currentStep, stale);
    resized();
    repaint();
}

void ChordSequenceComponent::CardContent::setRows (std::vector<LearningRow> newRows,
                                                    int newCurrentStep, bool isStale)
{
    if (newRows.size() == rows.size() && newCurrentStep == currentStep && isStale == stale)
    {
        auto unchanged = true;
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (rows[index].chord != newRows[index].chord
                || rows[index].degree != newRows[index].degree
                || rows[index].notes != newRows[index].notes
                || rows[index].durationBeats != newRows[index].durationBeats)
            {
                unchanged = false;
                break;
            }
        }

        if (unchanged)
            return;
    }

    rows = std::move (newRows);
    currentStep = newCurrentStep;
    stale = isStale;
    resized();
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

void ChordSequenceComponent::CardContent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    if (rows.empty())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (stale ? "Fix the progression above to build the sequence"
                          : "Enter chord symbols or Roman numerals to preview the sequence",
                    getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const auto column = static_cast<int> (index) % columnCount;
        const auto rowIndex = static_cast<int> (index) / columnCount;
        auto card = juce::Rectangle<int> (column * (cardWidth + sequenceCardGap),
                                          rowIndex * (sequenceCardHeight + sequenceCardGap),
                                          cardWidth, sequenceCardHeight).toFloat();
        const auto isCurrent = ! stale && static_cast<int> (index) == currentStep;

        g.setOpacity (stale ? 0.46f : 1.0f);
        g.setColour (isCurrent ? activeCardColour : panelColour);
        g.fillRoundedRectangle (card, 9.0f);
        g.setColour (isCurrent ? accentColour : outlineColour);
        g.drawRoundedRectangle (card, 9.0f, isCurrent ? 1.4f : 1.0f);

        if (isCurrent)
        {
            g.setColour (accentColour);
            g.fillRoundedRectangle (card.withHeight (4.0f).reduced (9.0f, 0.0f), 2.0f);
        }

        auto inner = card.toNearestInt().reduced (16, 12);
        auto titleRow = inner.removeFromTop (27);
        g.setColour (textColour);
        g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawFittedText (rows[index].chord, titleRow.removeFromLeft (titleRow.getWidth() - 52),
                          juce::Justification::centredLeft, 1);
        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (rows[index].degree, titleRow, juce::Justification::centredRight, 1);

        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText (rows[index].notes, inner.removeFromTop (35),
                          juce::Justification::centredLeft, 1);

        g.setColour (dimTextColour);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (formatBeats (rows[index].durationBeats) + " BEATS", inner,
                    juce::Justification::bottomLeft, false);
    }

    g.setOpacity (1.0f);
}

ChordTextAudioProcessorEditor::ChordTextAudioProcessorEditor (ChordTextAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p)
{
    setSize (editorWidth, editorHeight);
    setWantsKeyboardFocus (true);

    configureLabel (brandLabel, "CHORDSCRIPT", textColour, 14.0f);
    brandLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    configureLabel (productLabel, "MIDI GENERATOR", dimTextColour, 11.0f);
    configureLabel (transportStatusLabel, {}, dimTextColour, 11.0f);
    configureLabel (progressionLabel, "PROGRESSION", dimTextColour, 11.0f);
    configureLabel (progressionHintLabel, "Type chord symbols or Roman numerals",
                    dimTextColour, 11.0f);
    configureLabel (keyLabel, "KEY", dimTextColour, 11.0f);
    configureLabel (modeLabel, "MODE", dimTextColour, 11.0f);
    configureLabel (voicingLabel, "VOICING", dimTextColour, 11.0f);
    configureLabel (rangeLabel, "RANGE", dimTextColour, 11.0f);
    configureLabel (velocityLabel, "VELOCITY", dimTextColour, 11.0f);
    configureLabel (sequenceLabel, "CHORD SEQUENCE", dimTextColour, 11.0f);
    configureLabel (sequenceMetaLabel, {}, dimTextColour, 11.0f);
    configureLabel (sequenceStatusLabel, {}, accentColour, 11.0f);
    configureLabel (statusLabel, {}, dimTextColour, 11.0f);

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

    auto setUpCombo = [this] (juce::ComboBox& box, const char* parameterId)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (parameterId)))
            box.addItemList (choice->choices, 1);

        box.setColour (juce::ComboBox::backgroundColourId, panelColour);
        box.setColour (juce::ComboBox::textColourId, textColour);
        box.setColour (juce::ComboBox::outlineColourId, outlineColour);
        box.setColour (juce::ComboBox::arrowColourId, dimTextColour);
        addAndMakeVisible (box);
    };

    setUpCombo (keyBox, "key");
    setUpCombo (modeBox, "mode");
    setUpCombo (voicingBox, "voicing");
    setUpCombo (rangeBox, "range");

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
        applyProgressionText (true);

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
    sequenceView.setComponentID ("chordSequence");
    brandLabel.setComponentID ("brandLabel");
    transportStatusLabel.setComponentID ("transportStatus");
    statusLabel.setComponentID ("footerStatus");

    addAndMakeVisible (brandLabel);
    addAndMakeVisible (productLabel);
    addAndMakeVisible (transportStatusLabel);
    addAndMakeVisible (progressionLabel);
    addAndMakeVisible (progressionHintLabel);
    addAndMakeVisible (progressionEditor);
    addAndMakeVisible (keyLabel);
    addAndMakeVisible (modeLabel);
    addAndMakeVisible (voicingLabel);
    addAndMakeVisible (rangeLabel);
    addAndMakeVisible (velocityLabel);
    addAndMakeVisible (velocitySlider);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (exportButton);
    addAndMakeVisible (regenerateButton);
    addAndMakeVisible (revealClipButton);
    addAndMakeVisible (clipDrag);
    addAndMakeVisible (sequenceLabel);
    addAndMakeVisible (sequenceMetaLabel);
    addAndMakeVisible (sequenceStatusLabel);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (diagnosticsDisplay);
    addAndMakeVisible (sequenceView);

    keyAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "key", keyBox);
    modeAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "mode", modeBox);
    voicingAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "voicing",
                                                             voicingBox);
    rangeAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, "range", rangeBox);
    velocityAttachment = std::make_unique<SliderAttachment> (processor.apvts, "velocity",
                                                             velocitySlider);
    loopAttachment = std::make_unique<ButtonAttachment> (processor.apvts, "loop", loopButton);

    refreshStatus();
    startTimerHz (timerFrequencyHz);
}

ChordTextAudioProcessorEditor::~ChordTextAudioProcessorEditor()
{
    stopTimer();

    // Do not lose a pending debounced edit when the editor closes.
    if (textEditPending)
        applyProgressionText (false);

    fileChooser = nullptr;
}

void ChordTextAudioProcessorEditor::paint (juce::Graphics& g)
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
    auto area = getLocalBounds();
    const auto header = area.removeFromTop (58);
    const auto footer = area.removeFromBottom (48);

    brandLabel.setBounds (28, header.getY() + 15, 112, 24);
    productLabel.setBounds (145, header.getY() + 17, 140, 20);
    transportStatusLabel.setBounds (getWidth() - 270, header.getY() + 16, 242, 22);
    transportStatusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setBounds (footer.reduced (28, 0));

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

    placeControl (keyLabel, keyBox, 145);
    placeControl (modeLabel, modeBox, 150);
    placeControl (voicingLabel, voicingBox, 165);
    placeControl (rangeLabel, rangeBox, 125);
    const auto loopWidth = 60;
    const auto velocityWidth = std::max (80, controlFields.getRight() - x - loopWidth - gap);
    placeControl (velocityLabel, velocitySlider, velocityWidth);
    loopButton.setBounds (x, controlFields.getY(), loopWidth, controlFields.getHeight());

    constexpr int sequenceHeaderHeight = 40;
    constexpr int actionRowHeight = 44;
    constexpr int actionGap = 8;
    constexpr int desiredSequenceHeight = 160;
    const auto sequenceHeight = std::min (desiredSequenceHeight,
        std::max (1, content.getHeight() - sequenceHeaderHeight - actionRowHeight - actionGap));
    const auto flexibleSequenceGap = std::max (0, content.getHeight() - sequenceHeaderHeight
        - sequenceHeight - actionRowHeight - actionGap);
    content.removeFromTop (flexibleSequenceGap);

    auto sequenceHeader = content.removeFromTop (sequenceHeaderHeight);
    const auto sequenceHeaderY = sequenceHeader.getY();
    sequenceLabel.setBounds (sequenceHeader.removeFromTop (17));
    sequenceMetaLabel.setBounds (sequenceHeader);
    sequenceStatusLabel.setBounds (getWidth() - 228, sequenceHeaderY + 1, 200, 23);

    sequenceView.setBounds (content.removeFromTop (sequenceHeight));
    content.removeFromBottom (actionGap);

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
    const auto diagnostics = processor.getDiagnostics();
    const auto rows = processor.getLearningRows();
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

    const auto sequenceStatus = textEditPending ? juce::String ("Updating preview")
                                : ! diagnostics.empty()
                                    ? rows.empty() ? juce::String ("Fix progression errors")
                                                   : juce::String ("Last valid preview")
                                : rows.empty() ? juce::String ("Enter a progression")
                                               : juce::String::fromUTF8 ("\xe2\x9c\x93  Valid progression");
    if (sequenceStatusLabel.getText() != sequenceStatus)
        sequenceStatusLabel.setText (sequenceStatus, juce::dontSendNotification);
    const auto sequenceStatusColour = textEditPending ? juce::Colour (0xffd5aa61)
                                       : ! diagnostics.empty() ? errorColour
                                       : rows.empty() ? dimTextColour : accentColour;
    if (sequenceStatusLabel.findColour (juce::Label::textColourId) != sequenceStatusColour)
        sequenceStatusLabel.setColour (juce::Label::textColourId, sequenceStatusColour);

    double totalBeats = 0.0;

    for (const auto& row : rows)
        totalBeats += row.durationBeats;

    const auto sequenceMeta = rows.empty() ? juce::String ("No chords yet")
                               : juce::String (static_cast<int> (rows.size())) + " chords  \xc2\xb7  "
                                     + formatBeats (totalBeats) + " beats";
    if (sequenceMetaLabel.getText() != sequenceMeta)
        sequenceMetaLabel.setText (sequenceMeta, juce::dontSendNotification);
    sequenceView.setRows (rows, processor.getCurrentStep(),
                          textEditPending || ! diagnostics.empty());
    refreshTransportStatus();

    const bool canDrag = ! textEditPending && diagnostics.empty() && ! rows.empty();
    clipDrag.setLoopInfo (totalBeats, canDrag);

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
        resized();
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
                                            + " chords  Â·  MIDI ready";

    if (lastExportStatus.isNotEmpty())
        text += "  Â·  " + lastExportStatus;

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
