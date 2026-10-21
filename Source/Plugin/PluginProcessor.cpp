#include "PluginProcessor.h"

#include <cmath>

ChordTextAudioProcessor::ChordTextAudioProcessor()
    : juce::AudioProcessor (BusesProperties{})
{
}

void ChordTextAudioProcessor::prepareToPlay (double, int)
{
}

void ChordTextAudioProcessor::releaseResources()
{
}

void ChordTextAudioProcessor::processBlock (juce::AudioBuffer<float>& audio,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    midi.clear();

    auto* audioPlayHead = getPlayHead();
    if (audioPlayHead == nullptr)
        return;

    const auto position = audioPlayHead->getPosition();
    if (! position || ! position->getIsPlaying())
        return;

    const auto ppqPosition = position->getPpqPosition();
    if (! ppqPosition || ! std::isfinite (*ppqPosition))
        return;

    constexpr double beatsPerChord = 4.0;
    constexpr double boundaryTolerance = 1.0e-6;
    if (std::abs (std::remainder (*ppqPosition, beatsPerChord)) > boundaryTolerance)
        return;

    constexpr int pitches[] { 60, 64, 67 };
    for (const auto pitch : pitches)
        midi.addEvent (juce::MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (100)), 0);
}

juce::AudioProcessorEditor* ChordTextAudioProcessor::createEditor()
{
    return nullptr;
}

bool ChordTextAudioProcessor::hasEditor() const
{
    return false;
}

const juce::String ChordTextAudioProcessor::getName() const
{
    return "Chord Text";
}

bool ChordTextAudioProcessor::acceptsMidi() const
{
    return true;
}

bool ChordTextAudioProcessor::producesMidi() const
{
    return true;
}

bool ChordTextAudioProcessor::isMidiEffect() const
{
    return true;
}

double ChordTextAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ChordTextAudioProcessor::getNumPrograms()
{
    return 1;
}

int ChordTextAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ChordTextAudioProcessor::setCurrentProgram (int)
{
}

const juce::String ChordTextAudioProcessor::getProgramName (int)
{
    return {};
}

void ChordTextAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void ChordTextAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
}

void ChordTextAudioProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordTextAudioProcessor();
}
