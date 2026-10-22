#include "PluginEditor.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
constexpr int editorWidth = 760;
constexpr int editorHeight = 640;
constexpr int timerFrequencyHz = 30;
constexpr double debounceMilliseconds = 150.0;
constexpr int rowHeight = 24;

const auto backgroundColour = juce::Colour (0xff1b1b1d);
const auto panelColour = juce::Colour (0xff232326);
const auto outlineColour = juce::Colour (0xff3a3a3e);
const auto textColour = juce::Colour (0xffe4e4e6);
const auto dimTextColour = juce::Colour (0xffb0b0b4);
const auto errorColour = juce::Colour (0xffe07070);
const auto currentStepColour = juce::Colour (0xff3c5a7a);

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

ChordTextAudioProcessorEditor::ChordTextAudioProcessorEditor (ChordTextAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p)
{
    setSize (editorWidth, editorHeight);
    setWantsKeyboardFocus (true);

    configureLabel (progressionLabel, "Progression", dimTextColour, 13.0f);
    configureLabel (keyLabel, "Key", dimTextColour, 13.0f);
    configureLabel (modeLabel, "Mode", dimTextColour, 13.0f);
    configureLabel (voicingLabel, "Voicing", dimTextColour, 13.0f);
    configureLabel (rangeLabel, "Range", dimTextColour, 13.0f);
    configureLabel (velocityLabel, "Velocity", dimTextColour, 13.0f);
    configureLabel (statusLabel, {}, textColour, 13.0f);

    progressionEditor.setMultiLine (true, true);
    progressionEditor.setReturnKeyStartsNewLine (true);
    progressionEditor.setScrollbarsShown (true);
    progressionEditor.setComponentID ("progressionEditor");
    progressionEditor.setText (processor.getProgressionText(), juce::dontSendNotification);
    progressionEditor.setColour (juce::TextEditor::backgroundColourId, panelColour);
    progressionEditor.setColour (juce::TextEditor::textColourId, textColour);
    progressionEditor.setColour (juce::TextEditor::outlineColourId, outlineColour);
    progressionEditor.setColour (juce::TextEditor::focusedOutlineColourId,
                                 juce::Colours::cornflowerblue);
    progressionEditor.setFont (juce::Font (juce::FontOptions (14.0f)));
    progressionEditor.onTextChange = [this]
    {
        textEditPending = true;
        lastTextEditMs = juce::Time::getMillisecondCounterHiRes();
    };

    // Read-only, scrollable area: every diagnostic is shown, not just the
    // first two.
    diagnosticsDisplay.setMultiLine (true, true);
    diagnosticsDisplay.setScrollbarsShown (true);
    diagnosticsDisplay.setReadOnly (true);
    diagnosticsDisplay.setCaretVisible (false);
    diagnosticsDisplay.setPopupMenuEnabled (false);
    diagnosticsDisplay.setColour (juce::TextEditor::backgroundColourId, panelColour);
    diagnosticsDisplay.setColour (juce::TextEditor::textColourId, errorColour);
    diagnosticsDisplay.setColour (juce::TextEditor::outlineColourId, outlineColour);
    diagnosticsDisplay.setColour (juce::TextEditor::focusedOutlineColourId, outlineColour);
    diagnosticsDisplay.setFont (juce::Font (juce::FontOptions (13.0f)));

    auto setUpCombo = [this] (juce::ComboBox& box, const char* parameterId)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (parameterId)))
            box.addItemList (choice->choices, 1);

        box.setColour (juce::ComboBox::backgroundColourId, panelColour);
        box.setColour (juce::ComboBox::textColourId, textColour);
        box.setColour (juce::ComboBox::outlineColourId, outlineColour);
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
    velocitySlider.setColour (juce::Slider::trackColourId, juce::Colours::cornflowerblue);
    velocitySlider.setColour (juce::Slider::backgroundColourId, outlineColour);

    loopButton.setColour (juce::ToggleButton::textColourId, textColour);
    loopButton.setColour (juce::ToggleButton::tickColourId, juce::Colours::cornflowerblue);

    exportButton.onClick = [this] { startExport(); };
    exportButton.setEnabled (false);

    regenerateButton.setComponentID ("regenerateButton");
    regenerateButton.onClick = [this]
    {
        applyProgressionText (true);
        refreshStatus();
    };

    revealClipButton.setComponentID ("revealClip");
    revealClipButton.setEnabled (false);
    revealClipButton.onClick = [this]
    {
        const auto file = processor.getLastDragClipFile();

        if (file.existsAsFile())
            file.revealToUser();
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

    tableModel.setCurrentStep (-1);
    table.setModel (&tableModel);
    table.setComponentID ("learningTable");
    table.setRowHeight (rowHeight);
    table.setColour (juce::TableListBox::backgroundColourId, panelColour);
    table.setColour (juce::TableListBox::outlineColourId, outlineColour);
    table.setOutlineThickness (1);
    table.getHeader().addColumn ("Chord", 1, 150, 40, -1,
                                 juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn ("Degree", 2, 150, 40, -1,
                                 juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn ("Notes", 3, 300, 40, -1,
                                 juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn ("Beats", 4, 70, 40, -1,
                                 juce::TableHeaderComponent::notSortable);
    table.getHeader().setColour (juce::TableHeaderComponent::backgroundColourId,
                                 juce::Colour (0xff303034));
    table.getHeader().setColour (juce::TableHeaderComponent::textColourId, textColour);

    addAndMakeVisible (progressionLabel);
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
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (diagnosticsDisplay);
    addAndMakeVisible (table);

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
}

void ChordTextAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    auto progressionHeader = bounds.removeFromTop (22);
    regenerateButton.setBounds (progressionHeader.removeFromRight (110));
    progressionHeader.removeFromRight (8);
    progressionLabel.setBounds (progressionHeader);
    progressionEditor.setBounds (bounds.removeFromTop (104));
    bounds.removeFromTop (12);

    auto controlLabels = bounds.removeFromTop (18);
    auto controlFields = bounds.removeFromTop (28);
    bounds.removeFromTop (12);

    const int gap = 8;
    const int fieldWidth = (controlFields.getWidth() - 5 * gap) / 6;
    int x = controlFields.getX();

    auto placeControl = [&] (juce::Label& label, juce::Component& field)
    {
        label.setBounds (x, controlLabels.getY(), fieldWidth, controlLabels.getHeight());
        field.setBounds (x, controlFields.getY(), fieldWidth, controlFields.getHeight());
        x += fieldWidth + gap;
    };

    placeControl (keyLabel, keyBox);
    placeControl (modeLabel, modeBox);
    placeControl (voicingLabel, voicingBox);
    placeControl (rangeLabel, rangeBox);
    placeControl (velocityLabel, velocitySlider);
    loopButton.setBounds (x, controlFields.getY(), fieldWidth,
                          controlFields.getHeight());

    auto statusLabelRow = bounds.removeFromTop (20);
    statusLabel.setBounds (statusLabelRow);
    bounds.removeFromTop (4);

    // Actions get their own row so a long status/filename is not squeezed.
    auto actionRow = bounds.removeFromTop (28);
    exportButton.setBounds (actionRow.removeFromRight (140));
    actionRow.removeFromRight (8);
    revealClipButton.setBounds (actionRow.removeFromRight (140));
    bounds.removeFromTop (6);

    auto diagnosticsArea = bounds.removeFromBottom (68);
    bounds.removeFromBottom (6);

    clipDrag.setBounds (bounds.removeFromBottom (48));
    bounds.removeFromBottom (8);

    table.setBounds (bounds);
    diagnosticsDisplay.setBounds (diagnosticsArea);
}

void ChordTextAudioProcessorEditor::timerCallback()
{
    if (textEditPending
        && juce::Time::getMillisecondCounterHiRes() - lastTextEditMs >= debounceMilliseconds)
        applyProgressionText (false);

    if (tableModel.setRows (processor.getLearningRows()))
        table.updateContent();

    if (tableModel.setCurrentStep (processor.getCurrentStep()))
        table.repaint();

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
                safeThis->lastExportStatus = "Exported " + file.getFileName();
            else
                safeThis->lastExportStatus = "Export failed: " + error;
        }

        safeThis->fileChooser.reset();
        safeThis->refreshStatus();
    });
}

void ChordTextAudioProcessorEditor::refreshStatus()
{
    const auto statusText = buildStatusText();

    if (statusText != lastStatusText)
    {
        lastStatusText = statusText;
        statusLabel.setText (statusText, juce::dontSendNotification);
    }

    const auto diagnostics = processor.getDiagnostics();
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
    }

    const auto rows = processor.getLearningRows();
    double totalBeats = 0.0;

    for (const auto& row : rows)
        totalBeats += row.durationBeats;

    const bool canDrag = diagnostics.empty() && ! rows.empty();
    clipDrag.setLoopInfo (totalBeats, canDrag);

    // Check the last drag file only when its path changes instead of doing a
    // file-system stat on every 30 Hz timer tick. Reveal is session-only; an
    // external deletion of the file is an accepted edge case.
    const auto lastDragFile = processor.getLastDragClipFile();
    const auto lastDragPath = lastDragFile.getFullPathName();

    if (lastDragPath != lastRevealPath)
    {
        lastRevealPath = lastDragPath;
        lastRevealAvailable = lastDragPath.isNotEmpty() && lastDragFile.existsAsFile();
    }

    revealClipButton.setEnabled (lastRevealAvailable);

    const auto canExport = fileChooser == nullptr
                           && diagnostics.empty()
                           && ! rows.empty();

    if (exportButton.isEnabled() != canExport)
        exportButton.setEnabled (canExport);
}

juce::String ChordTextAudioProcessorEditor::buildStatusText() const
{
    juce::String text;

    if (processor.isTransportAvailable())
        text = "Transport ready";
    else if (processor.isPlayheadAvailable())
        text = "Transport stopped";
    else
        text = "No transport available";

    const auto rows = processor.getLearningRows();

    text += rows.empty() ? "  |  No chords"
                         : "  |  " + juce::String (static_cast<int> (rows.size())) + " chords";

    if (lastExportStatus.isNotEmpty())
        text += "  |  " + lastExportStatus;

    return text;
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

int ChordTextAudioProcessorEditor::LearningTableModel::getNumRows()
{
    return static_cast<int> (rows.size());
}

const LearningRow* ChordTextAudioProcessorEditor::LearningTableModel::rowFor (int rowNumber) const
{
    if (rowNumber < 0 || static_cast<std::size_t> (rowNumber) >= rows.size())
        return nullptr;

    return &rows[static_cast<std::size_t> (rowNumber)];
}

bool ChordTextAudioProcessorEditor::LearningTableModel::setRows (
    std::vector<LearningRow> newRows)
{
    if (newRows.size() == rows.size())
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
            return false;
    }

    rows = std::move (newRows);
    return true;
}

bool ChordTextAudioProcessorEditor::LearningTableModel::setCurrentStep (int step)
{
    if (step == currentStep)
        return false;

    currentStep = step;
    return true;
}

void ChordTextAudioProcessorEditor::LearningTableModel::paintRowBackground (
    juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected)
{
    if (rowNumber == currentStep)
        g.fillAll (currentStepColour);
    else if (rowIsSelected)
        g.fillAll (currentStepColour.withAlpha (0.5f));
    else
        g.fillAll (panelColour);
}

void ChordTextAudioProcessorEditor::LearningTableModel::paintCell (
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool)
{
    const auto* row = rowFor (rowNumber);

    if (row == nullptr)
        return;

    juce::String text;

    switch (columnId)
    {
        case 1: text = row->chord; break;
        case 2: text = row->degree; break;
        case 3: text = row->notes; break;
        case 4: text = formatBeats (row->durationBeats); break;
        default: break;
    }

    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}
