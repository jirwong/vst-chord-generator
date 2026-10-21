#include "Check.h"
#include "Plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    FakePlayHead head;
    head.set (0.0, true, 120.0);

    ChordTextAudioProcessor p;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (audio, midi);

    int noteOns = 0;
    int pitchSum = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            ++noteOns;
            pitchSum += message.getNoteNumber();
        }
    }

    CHECK (noteOns == 3);
    CHECK (pitchSum == 60 + 64 + 67);
    CHECK (midi.getFirstEventTime() == 0);

    return reportAndExit();
}
