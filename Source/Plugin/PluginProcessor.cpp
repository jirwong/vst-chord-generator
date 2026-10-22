#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Theory/Key.h"
#include "Theory/Progression.h"
#include "Theory/Voicer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double seekToleranceBeats = 0.25;
constexpr double beatEpsilon = 1.0e-9;

constexpr std::array<const char*, 17> keyNames {
    "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb",
    "G", "G#", "Ab", "A", "A#", "Bb", "B"
};

constexpr std::array<int, 3> rangeLowMidi { 36, 48, 60 };
constexpr std::array<int, 3> rangeHighMidi { 72, 84, 96 };

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

chordtext::PitchClass pitchClassFromName (const char* name)
{
    const auto* text = name;
    auto alteration = 0;

    if (text[1] == '#')
        alteration = 1;
    else if (text[1] == 'b')
        alteration = -1;

    return { text[0], alteration };
}

std::string noteNameForPitch (const chordtext::Chord& chord, int midi)
{
    const auto pitchClass = ((midi % 12) + 12) % 12;

    for (const auto& tone : chord.tones())
    {
        if (((tone.semitone() % 12) + 12) % 12 == pitchClass)
            return tone.name();
    }

    return juce::MidiMessage::getMidiNoteName (midi, true, false, 3).toStdString();
}

std::string notesForVoicing (const chordtext::Chord& chord, const std::vector<int>& voicing)
{
    std::string result;

    for (const auto pitch : voicing)
    {
        if (! result.empty())
            result += ' ';

        result += noteNameForPitch (chord, pitch);
    }

    return result;
}
} // namespace

ChordTextAudioProcessor::ChordTextAudioProcessor()
    : juce::AudioProcessor (BusesProperties{}),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    apvts.addParameterListener ("key", this);
    apvts.addParameterListener ("mode", this);
    apvts.addParameterListener ("voicing", this);
    apvts.addParameterListener ("range", this);
    apvts.addParameterListener ("velocity", this);
    apvts.addParameterListener ("loop", this);

    setProgressionText ("Cmaj7, Am7, Dm7, G7");
}

ChordTextAudioProcessor::~ChordTextAudioProcessor()
{
    cancelPendingUpdate();

    apvts.removeParameterListener ("key", this);
    apvts.removeParameterListener ("mode", this);
    apvts.removeParameterListener ("voicing", this);
    apvts.removeParameterListener ("range", this);
    apvts.removeParameterListener ("velocity", this);
    apvts.removeParameterListener ("loop", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
ChordTextAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "key", 1 }, "Key",
        juce::StringArray { "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb",
                            "G", "G#", "Ab", "A", "A#", "Bb", "B" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "Major", "Minor" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "voicing", 1 }, "Voicing",
        juce::StringArray { "Voice-led", "Root position" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "range", 1 }, "Range",
        juce::StringArray { "C2-C5", "C3-C6", "C4-C7" }, 1));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "velocity", 1 }, "Velocity", 1, 127, 100));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "loop", 1 }, "Loop", true));

    return layout;
}

void ChordTextAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "loop")
    {
        loopEnabled.store (newValue >= 0.5f, std::memory_order_relaxed);
        return;
    }

    triggerAsyncUpdate();
}

void ChordTextAudioProcessor::handleAsyncUpdate()
{
    rebuildPlan();
}

int ChordTextAudioProcessor::parameterIndex (const juce::String& parameterID) const
{
    if (const auto* value = apvts.getRawParameterValue (parameterID))
        return static_cast<int> (std::lround (value->load (std::memory_order_relaxed)));

    return 0;
}

chordtext::Key ChordTextAudioProcessor::selectedKey() const
{
    const auto keyIndex = std::clamp (parameterIndex ("key"), 0, 16);
    const auto mode = parameterIndex ("mode") == 1 ? chordtext::Mode::minor
                                                    : chordtext::Mode::major;

    return { pitchClassFromName (keyNames[static_cast<std::size_t> (keyIndex)]), mode };
}

void ChordTextAudioProcessor::setProgressionText (juce::String text)
{
    apvts.state.setProperty ("progressionText", text, nullptr);
    rebuildPlan();
}

juce::String ChordTextAudioProcessor::getProgressionText() const
{
    return apvts.state.getProperty ("progressionText").toString();
}

std::vector<chordtext::Diagnostic> ChordTextAudioProcessor::getDiagnostics() const
{
    return diagnostics;
}

std::vector<LearningRow> ChordTextAudioProcessor::getLearningRows() const
{
    return learningRows;
}

int ChordTextAudioProcessor::getCurrentStep() const
{
    return currentStep.load (std::memory_order_relaxed);
}

void ChordTextAudioProcessor::rebuildPlan()
{
    const auto key = selectedKey();
    const auto parsed = chordtext::parseProgression (getProgressionText().toStdString(), key);

    diagnostics = parsed.diagnostics;

    if (! parsed.progression.has_value())
        return;

    const auto voicingIndex = parameterIndex ("voicing");
    const auto rangeIndex = std::clamp (parameterIndex ("range"), 0, 2);

    chordtext::VoicingSettings settings;
    settings.mode = voicingIndex == 1 ? chordtext::VoicingMode::rootPosition
                                      : chordtext::VoicingMode::voiceLed;
    settings.lowMidi = rangeLowMidi[static_cast<std::size_t> (rangeIndex)];
    settings.highMidi = rangeHighMidi[static_cast<std::size_t> (rangeIndex)];

    const auto& progression = *parsed.progression;
    const auto voicings = chordtext::voiceProgression (progression, settings);
    const auto velocity = std::clamp (parameterIndex ("velocity"), 1, 127);

    publishPlan (chordtext::buildRenderPlan (progression, voicings, velocity));

    learningRows.clear();
    learningRows.reserve (progression.steps.size());

    for (std::size_t index = 0; index < progression.steps.size(); ++index)
    {
        const auto& step = progression.steps[index];
        LearningRow row;
        row.chord = step.chord.symbol();
        row.degree = chordtext::degreeLabel (step.chord, key);
        row.notes = notesForVoicing (step.chord, voicings[index]);
        row.durationBeats = step.durationBeats;
        learningRows.push_back (std::move (row));
    }
}

void ChordTextAudioProcessor::publishPlan (chordtext::RenderPlan plan)
{
    ++nextPlanGeneration;

    exportPlan = plan;

    auto& slot = planSlots[static_cast<std::size_t> (writePlanIndex)];
    slot.plan = std::move (plan);
    slot.generation = nextPlanGeneration;

    const auto previous = sharedPlanIndex.exchange (
        static_cast<std::uint32_t> (writePlanIndex) | freshPlanBit,
        std::memory_order_acq_rel);

    writePlanIndex = static_cast<int> (previous & planIndexMask);
}

bool ChordTextAudioProcessor::writeMidiFile (const juce::File& file, MidiFileMode mode,
                                             juce::String& errorMessage)
{
    errorMessage.clear();

    if (! diagnostics.empty())
    {
        errorMessage = "Cannot export while the progression has errors.";
        return false;
    }

    if (exportPlan.events.empty())
    {
        errorMessage = "Cannot export an empty progression.";
        return false;
    }

    constexpr double ticksPerQuarterNote = 960.0;

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (static_cast<int> (ticksPerQuarterNote));

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::textMetaEvent (3, "Chord Text"), 0.0);

    // Drag clips inherit the host project tempo, so they must not carry a
    // tempo or time-signature event. Only a standalone export is self-contained.
    if (mode == MidiFileMode::exportFile)
    {
        // getPlayHead()->getPosition() is only valid during processBlock. Use the
        // tempo cached by the audio thread, and fall back to 120 BPM until a block
        // has supplied a usable tempo.
        const auto cachedBpm = lastHostBpm.load (std::memory_order_relaxed);
        const auto bpm = cachedBpm > 0.0 && std::isfinite (cachedBpm) ? cachedBpm : 120.0;

        sequence.addEvent (juce::MidiMessage::tempoMetaEvent (
                               static_cast<int> (std::lround (60000000.0 / bpm))),
                           0.0);
        sequence.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    }

    auto events = exportPlan.events;
    std::stable_sort (events.begin(), events.end(),
                      [] (const chordtext::MidiEvent& left, const chordtext::MidiEvent& right)
                      {
                          if (left.beat != right.beat)
                              return left.beat < right.beat;

                          if (left.noteOn != right.noteOn)
                              return ! left.noteOn;

                          return left.pitch < right.pitch;
                      });

    for (const auto& event : events)
    {
        const auto tick = static_cast<double> (
            std::llround (event.beat * ticksPerQuarterNote));
        const auto message = event.noteOn
            ? juce::MidiMessage::noteOn (event.channel, event.pitch,
                                         static_cast<juce::uint8> (event.velocity))
            : juce::MidiMessage::noteOff (event.channel, event.pitch,
                                          static_cast<juce::uint8> (0));

        sequence.addEvent (message, tick);
    }

    midiFile.addTrack (sequence);

    juce::FileOutputStream output (file);

    if (! output.openedOk())
    {
        errorMessage = "Cannot open " + file.getFullPathName() + " for writing.";
        return false;
    }

    if (! midiFile.writeTo (output))
    {
        errorMessage = "Cannot write the MIDI file " + file.getFullPathName() + ".";
        return false;
    }

    return true;
}

bool ChordTextAudioProcessor::exportMidiFile (const juce::File& file,
                                              juce::String& errorMessage)
{
    return writeMidiFile (file, MidiFileMode::exportFile, errorMessage);
}

juce::File ChordTextAudioProcessor::createDragClipFile (juce::String& errorMessage)
{
    errorMessage.clear();

    if (! diagnostics.empty())
    {
        errorMessage = "Cannot create a clip while the progression has errors.";
        return {};
    }

    if (exportPlan.events.empty())
    {
        errorMessage = "Cannot create a clip for an empty progression.";
        return {};
    }

    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                            .getChildFile ("ChordText")
                            .getChildFile ("Clips");

    if (! folder.createDirectory())
    {
        errorMessage = "Cannot create the clips folder " + folder.getFullPathName() + ".";
        return {};
    }

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto file = folder.getNonexistentChildFile ("ChordText-" + stamp, ".mid", false);

    if (! writeMidiFile (file, MidiFileMode::dragClip, errorMessage))
    {
        // A failed write can leave a partial file behind. Remove it and keep
        // lastDragClipFile pointing at the last successful clip.
        file.deleteFile();
        return {};
    }

    lastDragClipFile = file;
    return file;
}

juce::File ChordTextAudioProcessor::getLastDragClipFile() const
{
    return lastDragClipFile;
}

void ChordTextAudioProcessor::cacheActivePitchesForRelease()
{
    std::uint64_t low = 0;
    std::uint64_t high = 0;

    for (auto pitch = 0; pitch < 128; ++pitch)
    {
        if (! activePitches[static_cast<std::size_t> (pitch)])
            continue;

        if (pitch < 64)
            low |= std::uint64_t { 1 } << pitch;
        else
            high |= std::uint64_t { 1 } << (pitch - 64);
    }

    // OR so a second prepare before the next block cannot lose an earlier
    // request.
    pendingReleaseLow.fetch_or (low, std::memory_order_relaxed);
    pendingReleaseHigh.fetch_or (high, std::memory_order_relaxed);
}

void ChordTextAudioProcessor::emitPendingReleases (juce::MidiBuffer& midi)
{
    const auto low = pendingReleaseLow.exchange (0, std::memory_order_acquire);
    const auto high = pendingReleaseHigh.exchange (0, std::memory_order_acquire);

    for (auto pitch = 0; pitch < 128; ++pitch)
    {
        const auto mask = std::uint64_t { 1 } << (pitch < 64 ? pitch : pitch - 64);
        const auto pending = pitch < 64 ? (low & mask) != 0 : (high & mask) != 0;

        if (! pending)
            continue;

        midi.addEvent (juce::MidiMessage::noteOff (1, pitch, static_cast<juce::uint8> (0)), 0);
        activePitches[static_cast<std::size_t> (pitch)] = false;
    }
}

void ChordTextAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    cacheActivePitchesForRelease();
    activePitches.fill (false);
    expectedNextBeat = 0.0;
    hasExpectedBeat = false;
    lastObservedGeneration = 0;
    currentStep.store (-1, std::memory_order_relaxed);
    transportAvailable.store (false, std::memory_order_relaxed);
}

void ChordTextAudioProcessor::releaseResources()
{
    // releaseResources cannot emit MIDI. Clearing the tracking is all this
    // callback can do; the host must flush any sounding notes it owns.
    activePitches.fill (false);
    pendingReleaseLow.store (0, std::memory_order_relaxed);
    pendingReleaseHigh.store (0, std::memory_order_relaxed);
    hasExpectedBeat = false;
    currentStep.store (-1, std::memory_order_relaxed);
    transportAvailable.store (false, std::memory_order_relaxed);
    playheadAvailable.store (false, std::memory_order_relaxed);
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
    // The host owns MidiBuffer capacity. JUCE's VST3 wrapper normally
    // preallocates 2048 bytes, but a zero-capacity host buffer can allocate on
    // the first addEvent. This is an accepted host-boundary residual; the
    // plugin itself never allocates or locks on the audio thread.
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

    // Releases requested by prepareToPlay must precede every new event.
    emitPendingReleases (midi);

    if ((sharedPlanIndex.load (std::memory_order_acquire) & freshPlanBit) != 0)
    {
        const auto previous = sharedPlanIndex.exchange (
            static_cast<std::uint32_t> (readPlanIndex), std::memory_order_acq_rel);
        readPlanIndex = static_cast<int> (previous & planIndexMask);
    }

    const auto& slot = planSlots[static_cast<std::size_t> (readPlanIndex)];
    const auto& plan = slot.plan;

    if (lastObservedGeneration != slot.generation)
    {
        lastObservedGeneration = slot.generation;
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
        playheadAvailable.store (false, std::memory_order_relaxed);
        disableLiveGeneration();
        return;
    }

    const auto position = hostPlayHead->getPosition();
    if (! position)
    {
        playheadAvailable.store (false, std::memory_order_relaxed);
        disableLiveGeneration();
        return;
    }

    playheadAvailable.store (true, std::memory_order_relaxed);

    // Cache the tempo while the playhead is legally readable. Export runs on
    // the message thread and must not call getPosition() itself.
    if (const auto hostBpm = position->getBpm();
        hostBpm && std::isfinite (*hostBpm) && *hostBpm > 0.0)
        lastHostBpm.store (*hostBpm, std::memory_order_relaxed);

    if (! position->getIsPlaying())
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
    const auto loopEnabledNow = loopEnabled.load (std::memory_order_relaxed);

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

    const auto phaseStart = loopEnabledNow
                                ? positiveFmod (blockStartBeat, loopLengthBeats)
                                : blockStartBeat;

    // Loop off: the progression plays once from song position 0.
    if (! loopEnabledNow && phaseStart >= loopLengthBeats)
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

    if (! loopEnabledNow)
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

    emitPendingReleases (midi);
    releaseActiveNotes (midi, 0);
    expectedNextBeat = 0.0;
    hasExpectedBeat = false;
}

bool ChordTextAudioProcessor::isTransportAvailable() const
{
    return transportAvailable.load (std::memory_order_relaxed);
}

bool ChordTextAudioProcessor::isPlayheadAvailable() const
{
    return playheadAvailable.load (std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ChordTextAudioProcessor::createEditor()
{
    return new ChordTextAudioProcessorEditor (*this);
}

bool ChordTextAudioProcessor::hasEditor() const
{
    return true;
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
    return "Default";
}

void ChordTextAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void ChordTextAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("progressionText", getProgressionText(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChordTextAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    // The plan rebuild owns message-thread-only state. Restoring state from a
    // host is expected on the message thread; if a host breaks that contract,
    // marshal the rebuild through the normal async path instead of touching
    // the plan directly.
    const auto onMessageThread = juce::MessageManager::existsAndIsCurrentThread();

    cancelPendingUpdate();
    apvts.replaceState (tree);
    cancelPendingUpdate();

    if (onMessageThread)
        rebuildPlan();
    else
        triggerAsyncUpdate();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordTextAudioProcessor();
}
