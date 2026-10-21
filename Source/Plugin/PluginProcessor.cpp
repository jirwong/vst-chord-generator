#include "PluginProcessor.h"

#include "Theory/Key.h"
#include "Theory/Progression.h"
#include "Theory/Voicer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
constexpr double seekToleranceBeats = 0.25;
constexpr double beatEpsilon = 1.0e-9;

double positiveFmod (double value, double modulus)
{
    const auto remainder = std::fmod (value, modulus);
    return remainder < 0.0 ? remainder + modulus : remainder;
}

// Finds the step whose note-ons have started at or before phase and whose
// note-offs have not all passed yet. Returns false when no chord is active.
bool findActiveStep (const chordtext::RenderPlan& plan, double phase,
                     double& stepStartBeat, int& stepIndex)
{
    stepStartBeat = 0.0;
    stepIndex = -1;

    for (const auto& event : plan.events)
    {
        if (! event.noteOn || event.beat > phase + beatEpsilon)
            continue;

        if (event.beat > stepStartBeat || stepIndex < 0)
        {
            stepStartBeat = event.beat;
            stepIndex = event.stepIndex;
        }
    }

    if (stepIndex < 0)
        return false;

    auto stepEndBeat = plan.lengthBeats;
    auto foundEnd = false;

    for (const auto& event : plan.events)
    {
        if (event.noteOn || event.stepIndex != stepIndex || event.beat <= stepStartBeat)
            continue;

        if (! foundEnd || event.beat < stepEndBeat)
        {
            stepEndBeat = event.beat;
            foundEnd = true;
        }
    }

    return ! foundEnd || phase < stepEndBeat;
}
} // namespace

ChordTextAudioProcessor::ChordTextAudioProcessor()
    : juce::AudioProcessor (BusesProperties{})
{
    const chordtext::Key key { chordtext::PitchClass { 'C', 0 }, chordtext::Mode::major };
    const auto parsed = chordtext::parseProgression ("Cmaj7, Am7, Dm7, G7", key);

    if (! parsed.progression.has_value())
        return;

    const chordtext::VoicingSettings settings {};
    publishPlan (chordtext::buildRenderPlan (*parsed.progression,
                                             chordtext::voiceProgression (*parsed.progression, settings),
                                             100));
}

void ChordTextAudioProcessor::publishPlan (chordtext::RenderPlan plan)
{
    const auto published = publishedPlan.load (std::memory_order_acquire);
    const auto observed = observedPlan.load (std::memory_order_acquire);

    auto slot = 0;
    while (slot == published || slot == observed)
        ++slot;

    planSlots[static_cast<std::size_t> (slot)] = std::move (plan);
    publishedPlan.store (slot, std::memory_order_release);
}

void ChordTextAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    activePitches.fill (false);
    expectedNextBeat = 0.0;
    hasExpectedBeat = false;
    lastObservedPlan = -1;
    currentStep.store (-1, std::memory_order_relaxed);
    transportAvailable.store (false, std::memory_order_relaxed);
}

void ChordTextAudioProcessor::releaseResources()
{
    activePitches.fill (false);
    hasExpectedBeat = false;
    currentStep.store (-1, std::memory_order_relaxed);
}

void ChordTextAudioProcessor::releaseActiveNotes (juce::MidiBuffer& midi, int sampleOffset)
{
    for (auto pitch = 0; pitch < 128; ++pitch)
    {
        if (! activePitches[static_cast<std::size_t> (pitch)])
            continue;

        midi.addEvent (juce::MidiMessage::noteOff (1, pitch, static_cast<juce::uint8> (0)), sampleOffset);
        activePitches[static_cast<std::size_t> (pitch)] = false;
    }

    currentStep.store (-1, std::memory_order_relaxed);
}

void ChordTextAudioProcessor::scheduleEvent (const chordtext::MidiEvent& event,
                                             juce::MidiBuffer& midi, int sampleOffset)
{
    if (event.noteOn)
    {
        midi.addEvent (juce::MidiMessage::noteOn (event.channel, event.pitch,
                                                  static_cast<juce::uint8> (event.velocity)),
                       sampleOffset);
        activePitches[static_cast<std::size_t> (event.pitch)] = true;
        currentStep.store (event.stepIndex, std::memory_order_relaxed);
    }
    else
    {
        midi.addEvent (juce::MidiMessage::noteOff (event.channel, event.pitch,
                                                   static_cast<juce::uint8> (0)),
                       sampleOffset);
        activePitches[static_cast<std::size_t> (event.pitch)] = false;
    }
}

void ChordTextAudioProcessor::processBlock (juce::AudioBuffer<float>& audio,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    midi.clear();

    const auto planIndex = publishedPlan.load (std::memory_order_acquire);
    observedPlan.store (planIndex, std::memory_order_release);
    const auto& plan = planSlots[static_cast<std::size_t> (planIndex)];

    if (lastObservedPlan != planIndex)
    {
        lastObservedPlan = planIndex;
        releaseActiveNotes (midi, 0);
        hasExpectedBeat = false;
    }

    auto disableLiveGeneration = [this, &midi]
    {
        releaseActiveNotes (midi, 0);
        expectedNextBeat = 0.0;
        hasExpectedBeat = false;
        transportAvailable.store (false, std::memory_order_relaxed);
    };

    auto* hostPlayHead = getPlayHead();
    if (hostPlayHead == nullptr)
    {
        disableLiveGeneration();
        return;
    }

    const auto position = hostPlayHead->getPosition();
    if (! position || ! position->getIsPlaying())
    {
        disableLiveGeneration();
        return;
    }

    const auto ppqPosition = position->getPpqPosition();
    const auto bpm = position->getBpm();

    if (! ppqPosition || ! bpm
        || ! std::isfinite (*ppqPosition) || ! std::isfinite (*bpm)
        || *bpm <= 0.0)
    {
        disableLiveGeneration();
        return;
    }

    transportAvailable.store (true, std::memory_order_relaxed);

    const auto numSamples = audio.getNumSamples();
    const auto blockStartBeat = *ppqPosition;
    const auto blockBeats = static_cast<double> (numSamples) * *bpm / (60.0 * currentSampleRate);
    const auto loopLengthBeats = plan.lengthBeats;

    if (loopLengthBeats <= 0.0)
    {
        releaseActiveNotes (midi, 0);
        expectedNextBeat = blockStartBeat + blockBeats;
        hasExpectedBeat = true;
        currentStep.store (-1, std::memory_order_relaxed);
        return;
    }

    const auto isSeek = hasExpectedBeat
                        && std::abs (blockStartBeat - expectedNextBeat) > seekToleranceBeats;
    const auto needsResync = ! hasExpectedBeat || isSeek;

    if (needsResync)
    {
        releaseActiveNotes (midi, 0);
        currentStep.store (-1, std::memory_order_relaxed);
    }

    const auto phaseStart = loopEnabled
                                ? positiveFmod (blockStartBeat, loopLengthBeats)
                                : blockStartBeat;

    // Loop off: the progression plays once from song position 0.
    if (! loopEnabled && phaseStart >= loopLengthBeats)
    {
        releaseActiveNotes (midi, 0);
        expectedNextBeat = blockStartBeat + blockBeats;
        hasExpectedBeat = true;
        currentStep.store (-1, std::memory_order_relaxed);
        return;
    }

    const auto samplesPerBeat = currentSampleRate * 60.0 / *bpm;
    const auto maxSampleOffset = std::max (0, numSamples - 1);

    auto offsetForRelativeBeat = [samplesPerBeat, maxSampleOffset] (double relativeBeat)
    {
        return std::clamp (static_cast<int> (std::lround (relativeBeat * samplesPerBeat)),
                           0, maxSampleOffset);
    };

    // Starting or seeking into the middle of a chord retriggers it at sample 0.
    double activeStepStartBeat = 0.0;
    auto activeStepIndex = -1;

    if (needsResync
        && findActiveStep (plan, phaseStart, activeStepStartBeat, activeStepIndex)
        && phaseStart > activeStepStartBeat + beatEpsilon)
    {
        for (const auto& event : plan.events)
        {
            if (event.noteOn
                && event.stepIndex == activeStepIndex
                && event.beat == activeStepStartBeat)
            {
                scheduleEvent (event, midi, 0);
            }
        }
    }

    auto scheduleSegment = [&] (double phaseFrom, double phaseTo, double relativeStart)
    {
        for (const auto& event : plan.events)
        {
            if (event.beat < phaseFrom || event.beat >= phaseTo)
                continue;

            scheduleEvent (event, midi,
                           offsetForRelativeBeat (relativeStart + (event.beat - phaseFrom)));
        }
    };

    auto remainingBeats = blockBeats;
    auto phase = phaseStart;
    auto relativeStart = 0.0;

    if (! loopEnabled)
    {
        const auto phaseEnd = std::min (loopLengthBeats, phaseStart + blockBeats);
        scheduleSegment (phaseStart, phaseEnd, 0.0);

        if (phaseStart + blockBeats > loopLengthBeats + beatEpsilon)
            releaseActiveNotes (midi, offsetForRelativeBeat (loopLengthBeats - phaseStart));
    }
    else
    {
        for (auto segment = 0; segment < 64 && remainingBeats > beatEpsilon; ++segment)
        {
            const auto toLoopEnd = loopLengthBeats - phase;

            if (remainingBeats < toLoopEnd - beatEpsilon)
            {
                scheduleSegment (phase, phase + remainingBeats, relativeStart);
                remainingBeats = 0.0;
            }
            else
            {
                scheduleSegment (phase, loopLengthBeats, relativeStart);
                relativeStart += toLoopEnd;
                remainingBeats -= toLoopEnd;
                phase = 0.0;
                releaseActiveNotes (midi, offsetForRelativeBeat (relativeStart));
            }
        }

        if (remainingBeats > beatEpsilon)
            releaseActiveNotes (midi, maxSampleOffset);
    }

    auto anyActive = false;
    for (const auto active : activePitches)
    {
        if (active)
        {
            anyActive = true;
            break;
        }
    }

    if (! anyActive)
        currentStep.store (-1, std::memory_order_relaxed);

    expectedNextBeat = blockStartBeat + blockBeats;
    hasExpectedBeat = true;
}

void ChordTextAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& audio,
                                                    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    midi.clear();

    releaseActiveNotes (midi, 0);
    expectedNextBeat = 0.0;
    hasExpectedBeat = false;
}

bool ChordTextAudioProcessor::isTransportAvailable() const
{
    return transportAvailable.load (std::memory_order_relaxed);
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
