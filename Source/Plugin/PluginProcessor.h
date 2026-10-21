#pragma once

#include "Theory/RenderPlan.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

class ChordTextAudioProcessor final : public juce::AudioProcessor
{
public:
    ChordTextAudioProcessor();
    ~ChordTextAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isTransportAvailable() const;

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
    // Message thread only. Never call from processBlock.
    void publishPlan (chordtext::RenderPlan plan);

    void releaseActiveNotes (juce::MidiBuffer& midi, int sampleOffset);
    void scheduleEvent (const chordtext::MidiEvent& event, juce::MidiBuffer& midi, int sampleOffset);

    static constexpr int numPlanSlots = 3;

    std::array<chordtext::RenderPlan, numPlanSlots> planSlots;
    std::atomic<int> publishedPlan { 0 };
    std::atomic<int> observedPlan { 0 };
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> transportAvailable { false };

    std::array<bool, 128> activePitches {};
    double currentSampleRate = 48000.0;
    double expectedNextBeat = 0.0;
    bool hasExpectedBeat = false;
    int lastObservedPlan = -1;
    bool loopEnabled = true;
};
