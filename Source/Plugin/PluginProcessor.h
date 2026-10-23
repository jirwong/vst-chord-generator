#pragma once

#include "Theory/Diagnostic.h"
#include "Theory/RenderPlan.h"
#include "Plugin/ChordPreview.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

struct LearningRow
{
    juce::String chord;
    juce::String degree;
    juce::String notes;
    double durationBeats = 4.0;
    int numericDegree = 0;
};

class ChordTextAudioProcessor final : public juce::AudioProcessor,
                                      public juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AsyncUpdater
{
public:
    ChordTextAudioProcessor();
    ~ChordTextAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isTransportAvailable() const;
    // True when the last processed block had a playhead position, even if the
    // transport was stopped. Lets the editor distinguish "stopped" from
    // "no playhead".
    bool isPlayheadAvailable() const;

    juce::AudioProcessorValueTreeState apvts;

    void setProgressionText (juce::String text);
    juce::String getProgressionText() const;
    std::vector<chordtext::Diagnostic> getDiagnostics() const;
    std::vector<LearningRow> getLearningRows() const;
    int getCurrentStep() const;
    juce::String getScaleSummary() const;
    void rebuildPlan(); // message thread only

    // Chord-card preview. Gesture methods run on the message thread and
    // return a non-zero gesture ID only when the card is eligible.
    chordscript::PreviewGestureId beginMomentaryPreview (
        int stepIndex, std::uint64_t displayedRevision);
    void endMomentaryPreview (chordscript::PreviewGestureId gestureId);
    void toggleHeldPreview (int stepIndex, std::uint64_t displayedRevision);
    chordscript::PreviewFeedback getPreviewFeedback();
    std::uint64_t getPreviewCardRevision() const noexcept;
    bool isPlanRebuildPending() const noexcept;
    void cancelChordPreview() noexcept;
    void setPreviewInputPending (bool pending);

    // Message thread only. Uses the same plan as live playback.
    bool exportMidiFile (const juce::File& file, juce::String& errorMessage);

    // Message thread only. Writes a drag-and-drop clip to the user's
    // Documents\ChordText\Clips folder.
    juce::File createDragClipFile (juce::String& errorMessage);
    // Message thread only. Same writer with an explicit destination folder.
    juce::File createDragClipFile (juce::String& errorMessage,
                                   const juce::File& outputDirectory);
    juce::File getLastDragClipFile() const;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    // Message thread only. Never call from processBlock.
    void publishPlan (chordtext::RenderPlan plan);

    // Message thread only.
    bool isPreviewCardEligible (int stepIndex, std::uint64_t displayedRevision) const noexcept;
    chordscript::PreviewRequest makeIdlePreviewRequest() const noexcept;
    void publishPreviewRequest (const chordscript::PreviewRequest& request);
    // Audio thread only.
    void publishPreviewFeedback (const chordscript::PreviewRequest& request);

    enum class MidiFileMode { exportFile, dragClip };

    // Message thread only.
    bool writeMidiFile (const juce::File& file, MidiFileMode mode,
                        juce::String& errorMessage);

    chordtext::Key selectedKey() const;
    int parameterIndex (const juce::String& parameterID) const;

    void releaseActiveNotes (juce::MidiBuffer& midi, int sampleOffset);
    void scheduleEvent (const chordtext::MidiEvent& event, juce::MidiBuffer& midi, int sampleOffset);
    void cacheActivePitchesForRelease();
    void emitPendingReleases (juce::MidiBuffer& midi);

    static constexpr int numPlanSlots = 3;
    static constexpr std::uint32_t planIndexMask = 0x00ffffffu;
    static constexpr std::uint32_t freshPlanBit = 0x01000000u;

    struct PlanSlot
    {
        chordtext::RenderPlan plan;
        std::uint64_t generation = 0;
    };

    // Triple buffer: the message thread owns writePlanIndex, the audio thread
    // owns readPlanIndex, and sharedPlanIndex holds the slot in between.
    std::array<PlanSlot, numPlanSlots> planSlots;
    std::atomic<std::uint32_t> sharedPlanIndex { 2 };
    int readPlanIndex = 0;
    int writePlanIndex = 1;
    std::uint64_t nextPlanGeneration = 0;
    std::uint64_t lastObservedGeneration = 0;

    std::atomic<int> currentStep { -1 };
    std::atomic<bool> transportAvailable { false };
    std::atomic<bool> playheadAvailable { false };
    std::atomic<bool> loopEnabled { true };
    // Latest external mode event. Both legacy and new automation write here;
    // only the message thread synchronizes APVTS and rebuilds the plan.
    std::atomic<int> effectiveScaleIndex { 0 };

    // Written only by the audio thread, read by the message thread for export.
    // AudioPlayHead::getPosition() is processBlock-only, so export must never
    // query the playhead itself.
    std::atomic<double> lastHostBpm { 0.0 };

    std::array<bool, 128> activePitches {};
    double currentSampleRate = 48000.0;
    double expectedNextBeat = 0.0;
    bool hasExpectedBeat = false;

    // Pitches sounding when prepareToPlay last reset the schedule. The host may
    // call prepareToPlay off the audio thread, so these masks carry the release
    // request to the start of the next block. Bits 0-63 and 64-127.
    std::atomic<std::uint64_t> pendingReleaseLow { 0 };
    std::atomic<std::uint64_t> pendingReleaseHigh { 0 };

    // Message thread only.
    std::vector<chordtext::Diagnostic> diagnostics;
    std::vector<LearningRow> learningRows;
    chordtext::RenderPlan exportPlan;
    juce::File lastDragClipFile;

    // Request channel: message thread writes, audio thread reads.
    // Feedback channel: audio thread writes, message thread reads.
    chordscript::ChordPreviewExchange previewExchange;
    std::atomic<std::uint64_t> previewCancellationEpoch { 0 };
    std::atomic<std::uint64_t> previewDataRevision { 0 };
    std::atomic<bool> planRebuildPending { false };

    // Message thread only.
    std::uint64_t previewCacheRevision = 0;
    std::vector<std::array<std::uint64_t, 2>> previewPitchMasks;
    int previewVelocity = 100;
    bool previewInputPending = false;
    chordscript::PreviewGestureId nextPreviewGestureId = 1;
    chordscript::PreviewRequest desiredPreviewRequest;
    chordscript::PreviewFeedback cachedPreviewFeedback;

    // Audio thread only.
    chordscript::PreviewRequest audioPreviewRequest;
    chordscript::PreviewRequest appliedPreviewRequest;
    bool bypassInterval = false;
};
