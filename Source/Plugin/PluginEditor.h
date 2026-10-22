#pragma once

#include "ClipDragComponent.h"
#include "PluginProcessor.h"

#include <memory>
#include <vector>

class ChordTextAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit ChordTextAudioProcessorEditor (ChordTextAudioProcessor&);
    ~ChordTextAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LearningTableModel final : public juce::TableListBoxModel
    {
    public:
        int getNumRows() override;
        void paintRowBackground (juce::Graphics&, int rowNumber, int width, int height,
                                 bool rowIsSelected) override;
        void paintCell (juce::Graphics&, int rowNumber, int columnId, int width, int height,
                        bool rowIsSelected) override;

        // Returns true when the visible content changed.
        bool setRows (std::vector<LearningRow> newRows);
        // Returns true when the highlighted row changed.
        bool setCurrentStep (int step);

    private:
        const LearningRow* rowFor (int rowNumber) const;

        std::vector<LearningRow> rows;
        int currentStep = -1;
    };

    void timerCallback() override;
    void applyProgressionText (bool force);
    void startExport();
    void refreshStatus();

    juce::String buildStatusText() const;
    juce::String describeDiagnostic (const chordtext::Diagnostic&) const;

    ChordTextAudioProcessor& processor;

    juce::Label progressionLabel;
    juce::TextEditor progressionEditor;
    juce::Label keyLabel;
    juce::Label modeLabel;
    juce::Label voicingLabel;
    juce::Label rangeLabel;
    juce::Label velocityLabel;
    juce::ComboBox keyBox;
    juce::ComboBox modeBox;
    juce::ComboBox voicingBox;
    juce::ComboBox rangeBox;
    juce::Slider velocitySlider;
    juce::ToggleButton loopButton { "Loop" };
    juce::TextButton exportButton { "Export .mid" };
    juce::TextButton regenerateButton { "Regenerate" };
    juce::TextButton revealClipButton { "Reveal last clip" };
    ClipDragComponent clipDrag;
    juce::Label statusLabel;
    juce::TextEditor diagnosticsDisplay;
    LearningTableModel tableModel;
    juce::TableListBox table;

    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Declared after the components so the attachments are detached before
    // their controls are destroyed.
    std::unique_ptr<ComboBoxAttachment> keyAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    std::unique_ptr<ComboBoxAttachment> voicingAttachment;
    std::unique_ptr<ComboBoxAttachment> rangeAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<ButtonAttachment> loopAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String lastExportStatus;
    juce::String lastStatusText;
    juce::String lastDiagnosticsText;
    juce::String lastRevealPath;
    bool lastRevealAvailable = false;
    bool textEditPending = false;
    double lastTextEditMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordTextAudioProcessorEditor)
};
