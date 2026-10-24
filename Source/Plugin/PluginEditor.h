#pragma once

#include "ClipDragComponent.h"
#include "PluginProcessor.h"
#include "UiSettings.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class ChordSequenceComponent final : public juce::Viewport
{
public:
    ChordSequenceComponent();
    ~ChordSequenceComponent() override;

    void resized() override;
    void setRows (std::vector<LearningRow> newRows, int currentStep, bool stale,
                  std::uint64_t displayedRevision);
    void setPreviewFeedback (chordscript::PreviewFeedback feedback);

    juce::Rectangle<int> cardBounds (int index) const;
    int cardAt (juce::Point<int> point) const;
    std::uint64_t getDisplayedRevision() const;

    std::function<chordscript::PreviewGestureId (int, std::uint64_t)> onMomentaryPreview;
    std::function<void (chordscript::PreviewGestureId)> onMomentaryRelease;
    std::function<void (int, std::uint64_t)> onToggleHeldPreview;

private:
    class CardContent final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void setRows (std::vector<LearningRow> newRows, int currentStep, bool stale,
                      std::uint64_t displayedRevision);
        void setPreviewFeedback (chordscript::PreviewFeedback feedback);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        int getContentHeightForWidth (int width) const;
        juce::Rectangle<int> cardBounds (int index) const;
        int cardAt (juce::Point<int> point) const;
        void setOwner (ChordSequenceComponent* newOwner) { owner = newOwner; }
        std::uint64_t getDisplayedRevision() const { return displayedRevision; }

    private:
        std::vector<LearningRow> rows;
        int currentStep = -1;
        bool stale = false;
        std::uint64_t displayedRevision = 0;
        chordscript::PreviewFeedback previewFeedback;
        std::vector<chordscript::PreviewGestureId> momentaryGestureIds;
        ChordSequenceComponent* owner = nullptr;
        int columnCount = 1;
        int cardWidth = 0;
    };

    CardContent content;
};

class ChordTextAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit ChordTextAudioProcessorEditor (ChordTextAudioProcessor&, UiSettings settings = UiSettings{});
    ~ChordTextAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ContentComponent final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    void layoutContent();
    void applyUiScale (int percent);
    void timerCallback() override;
    void applyProgressionText (bool force);
    void startExport();
    void refreshStatus();

    juce::String buildStatusText() const;
    void refreshTransportStatus();
    juce::String describeDiagnostic (const chordtext::Diagnostic&) const;

    ChordTextAudioProcessor& processor;
    UiSettings uiSettings;
    int appliedUiScalePercent = 100;
    juce::String uiScaleSaveError;
    ContentComponent editorContent;
    juce::Label uiScaleLabel;
    juce::ComboBox uiScaleBox;

    juce::Label brandLabel;
    juce::Label productLabel;
    juce::Label transportStatusLabel;
    juce::Label progressionLabel;
    juce::Label progressionHintLabel;
    juce::TextEditor progressionEditor;
    juce::Label keyLabel;
    juce::Label modeLabel;
    juce::Label voicingLabel;
    juce::Label rangeLabel;
    juce::Label velocityLabel;
    juce::Label scaleSummaryLabel;
    juce::Label defaultLengthLabel;
    juce::Label bassLabel;
    juce::ComboBox keyBox;
    juce::ComboBox modeBox;
    juce::ComboBox voicingBox;
    juce::ComboBox rangeBox;
    juce::ComboBox defaultLengthBox;
    juce::ComboBox bassBox;
    juce::Slider velocitySlider;
    juce::ToggleButton loopButton { "Loop" };
    juce::TextButton exportButton { "Export MIDI" };
    juce::TextButton regenerateButton { "Regenerate" };
    juce::TextButton revealClipButton { "Reveal file" };
    ClipDragComponent clipDrag;
    juce::Label sequenceLabel;
    juce::Label sequenceMetaLabel;
    juce::Label previewHintLabel;
    juce::Label sequenceStatusLabel;
    juce::Label statusLabel;
    juce::TextEditor diagnosticsDisplay;
    ChordSequenceComponent sequenceView;
    juce::TooltipWindow tooltipWindow;

    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Declared after the components so the attachments are detached before
    // their controls are destroyed.
    std::unique_ptr<ComboBoxAttachment> keyAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    std::unique_ptr<ComboBoxAttachment> voicingAttachment;
    std::unique_ptr<ComboBoxAttachment> rangeAttachment;
    std::unique_ptr<ComboBoxAttachment> defaultLengthAttachment;
    std::unique_ptr<ComboBoxAttachment> bassAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<ButtonAttachment> loopAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String lastExportStatus;
    juce::String lastStatusText;
    juce::String lastDiagnosticsText;
    juce::String lastRevealPath;
    juce::File latestOutputFile;
    bool lastRevealAvailable = false;
    bool textEditPending = false;
    bool lastTextEditPending = false;
    double lastTextEditMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordTextAudioProcessorEditor)
};
