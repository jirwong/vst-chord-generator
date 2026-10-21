#include "Check.h"
#include "Plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

class FakePlayHead : public juce::AudioPlayHead
{
public:
    void set (double ppqIn, bool playingIn, double bpmIn)
    {
        ppq = ppqIn;
        playing = playingIn;
        bpm = bpmIn;
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        return info;
    }

private:
    double ppq = 0.0;
    bool playing = true;
    double bpm = 120.0;
};

static std::vector<int> noteOnPitches (const juce::MidiBuffer& midi)
{
    std::vector<int> result;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            result.push_back (metadata.getMessage().getNoteNumber());
    return result;
}

static int countNoteOffs (const juce::MidiBuffer& midi)
{
    int result = 0;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOff())
            ++result;
    return result;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Walking-skeleton smoke test, updated for the parsed default plan.
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);

        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        const auto pitches = noteOnPitches (midi);
        int pitchSum = 0;
        for (const auto pitch : pitches)
            pitchSum += pitch;

        CHECK (pitches.size() == 4);
        CHECK (pitchSum == 48 + 52 + 55 + 59);
        CHECK (midi.getFirstEventTime() == 0);
    }

    // first chord at transport start
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        CHECK ((noteOnPitches (midi) == std::vector<int> {48, 52, 55, 59}));
    }

    // stop releases active notes
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK (noteOnPitches (midi).size() == 4);

        head.set (0.02, false, 120.0);
        juce::MidiBuffer stopped;
        p.processBlock (audio, stopped);
        CHECK (countNoteOffs (stopped) == 4);
        CHECK (noteOnPitches (stopped).empty());
    }

    // bypass releases active notes
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        juce::MidiBuffer bypassed;
        p.processBlockBypassed (audio, bypassed);
        CHECK (countNoteOffs (bypassed) == 4);
    }

    // seek releases old notes and retriggers the active chord
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer first;
        p.processBlock (audio, first);

        head.set (8.0, true, 120.0);
        juce::MidiBuffer sought;
        p.processBlock (audio, sought);
        CHECK (countNoteOffs (sought) >= 4);
        CHECK (! noteOnPitches (sought).empty());
    }

    // loop-boundary ordering: note-offs before note-ons at beat 4
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        const double blockBeats = 512.0 * 120.0 / (60.0 * 48000.0);

        // Advance without a seek until the block that contains beat 4.
        for (int i = 0; i < 187; ++i)
        {
            head.set (i * blockBeats, true, 120.0);
            juce::MidiBuffer discarded;
            p.processBlock (audio, discarded);
        }

        head.set (187 * blockBeats, true, 120.0);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        int lastOffIndex = -1, firstOnIndex = -1, index = 0;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOff()) lastOffIndex = index;
            if (message.isNoteOn() && firstOnIndex < 0) firstOnIndex = index;
            ++index;
        }

        CHECK (lastOffIndex >= 0);
        CHECK (firstOnIndex >= 0);
        CHECK (lastOffIndex < firstOnIndex);
    }

    return reportAndExit();
}
