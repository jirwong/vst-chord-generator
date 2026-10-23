#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Theory/Chord.h"
#include "Theory/Key.h"
#include "Theory/MidiFileWriter.h"
#include "Theory/Progression.h"
#include "Theory/Voicer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
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

// Internal parameter echoes are synchronous on the calling thread. A thread-local
// guard does not suppress a real automation event arriving on another thread.
thread_local const ChordTextAudioProcessor* synchronizingProcessor = nullptr;

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
} // namespace

ChordTextAudioProcessor::ChordTextAudioProcessor()
    : juce::AudioProcessor (BusesProperties{}),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    apvts.addParameterListener ("key", this);
    apvts.addParameterListener ("mode", this);
    apvts.addParameterListener ("scaleMode", this);
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
    apvts.removeParameterListener ("scaleMode", this);
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

    juce::StringArray scaleNames;
    for (const auto mode : chordtext::allModes())
        scaleNames.add (juce::String (chordtext::modeDisplayName (mode).data()));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "scaleMode", 1 }, "Scale Mode", scaleNames, 0));

    return layout;
}

void ChordTextAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "mode" || parameterID == "scaleMode")
    {
        if (synchronizingProcessor == this)
            return;
        const auto maximum = parameterID == "mode" ? 1 : 8;
        effectiveScaleIndex.store (std::clamp (static_cast<int> (std::lround (newValue)), 0, maximum),
                                   std::memory_order_relaxed);
    }
    if (parameterID == "loop")
    {
        loopEnabled.store (newValue >= 0.5f, std::memory_order_relaxed);
        // Loop changes reuse the voicing snapshot but still end any gesture.
        cancelChordPreview();
        return;
    }

    const auto requiresRebuiltVoicings = parameterID == "key"
                                         || parameterID == "mode"
                                         || parameterID == "scaleMode"
                                         || parameterID == "voicing"
                                         || parameterID == "range"
                                         || parameterID == "velocity";

    if (requiresRebuiltVoicings)
    {
        planRebuildPending.store (true, std::memory_order_release);
        previewDataRevision.fetch_add (1, std::memory_order_acq_rel);
    }

    cancelChordPreview();
    triggerAsyncUpdate();
}

void ChordTextAudioProcessor::handleAsyncUpdate()
{
    const auto selected = effectiveScaleIndex.load (std::memory_order_relaxed);
    {
        const juce::ScopedValueSetter<const ChordTextAudioProcessor*> guard (synchronizingProcessor, this);
        auto* scale = apvts.getParameter ("scaleMode");
        if (parameterIndex ("scaleMode") != selected)
            scale->setValueNotifyingHost (scale->convertTo0to1 (static_cast<float> (selected)));
        if (selected <= 1 && parameterIndex ("mode") != selected)
            apvts.getParameter ("mode")->setValueNotifyingHost (static_cast<float> (selected));
    }
    rebuildPlan();
    if (effectiveScaleIndex.load (std::memory_order_relaxed) != selected)
        triggerAsyncUpdate();
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
    const auto mode = chordtext::allModes()[static_cast<std::size_t> (
        std::clamp (effectiveScaleIndex.load (std::memory_order_relaxed), 0, 8))];

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

juce::String ChordTextAudioProcessor::getScaleSummary() const
{
    const auto key = selectedKey();
    auto summary = juce::String (key.tonic().name()) + " "
                 + juce::String (chordtext::modeDisplayName (key.mode()).data())
                 + juce::String::fromUTF8 (" \xc2\xb7");
    for (const auto& note : key.scaleNotes())
        summary += " " + juce::String (note.name());
    return summary;
}

std::uint64_t ChordTextAudioProcessor::getPreviewCardRevision() const noexcept
{
    if (previewInputPending || previewCacheRevision == 0 || previewPitchMasks.empty())
        return 0;

    if (previewCacheRevision != previewDataRevision.load (std::memory_order_acquire))
        return 0;

    return previewCacheRevision;
}

bool ChordTextAudioProcessor::isPlanRebuildPending() const noexcept
{
    return planRebuildPending.load (std::memory_order_acquire);
}

bool ChordTextAudioProcessor::isPreviewCardEligible (int stepIndex,
                                                     std::uint64_t displayedRevision) const noexcept
{
    if (previewInputPending || displayedRevision == 0 || displayedRevision != previewCacheRevision)
        return false;

    if (previewCacheRevision != previewDataRevision.load (std::memory_order_acquire))
        return false;

    return stepIndex >= 0 && static_cast<std::size_t> (stepIndex) < previewPitchMasks.size();
}

chordscript::PreviewRequest ChordTextAudioProcessor::makeIdlePreviewRequest() const noexcept
{
    chordscript::PreviewRequest request;
    request.mode = chordscript::PreviewMode::idle;
    request.cancellationEpoch = previewCancellationEpoch.load (std::memory_order_acquire);
    request.dataRevision = previewDataRevision.load (std::memory_order_acquire);
    return request;
}

void ChordTextAudioProcessor::publishPreviewRequest (const chordscript::PreviewRequest& request)
{
    desiredPreviewRequest = request;
    previewExchange.publishRequest (request);
}

chordscript::PreviewGestureId ChordTextAudioProcessor::beginMomentaryPreview (
    int stepIndex, std::uint64_t displayedRevision)
{
    if (! isPreviewCardEligible (stepIndex, displayedRevision))
        return 0;

    const auto gestureId = nextPreviewGestureId++;
    if (nextPreviewGestureId == 0)
        nextPreviewGestureId = 1;

    chordscript::PreviewRequest request;
    request.mode = chordscript::PreviewMode::momentary;
    request.stepIndex = stepIndex;
    request.gestureId = gestureId;
    request.cancellationEpoch = previewCancellationEpoch.load (std::memory_order_acquire);
    request.dataRevision = previewCacheRevision;
    request.pitches = previewPitchMasks[static_cast<std::size_t> (stepIndex)];
    request.velocity = previewVelocity;
    publishPreviewRequest (request);
    return gestureId;
}

void ChordTextAudioProcessor::endMomentaryPreview (chordscript::PreviewGestureId gestureId)
{
    if (desiredPreviewRequest.mode == chordscript::PreviewMode::momentary
        && desiredPreviewRequest.gestureId == gestureId)
    {
        publishPreviewRequest (makeIdlePreviewRequest());
    }
}

void ChordTextAudioProcessor::toggleHeldPreview (int stepIndex, std::uint64_t displayedRevision)
{
    if (previewInputPending)
        return;

    const auto epoch = previewCancellationEpoch.load (std::memory_order_acquire);
    if (desiredPreviewRequest.mode == chordscript::PreviewMode::held
        && desiredPreviewRequest.cancellationEpoch == epoch
        && desiredPreviewRequest.dataRevision == previewCacheRevision
        && desiredPreviewRequest.stepIndex == stepIndex)
    {
        publishPreviewRequest (makeIdlePreviewRequest());
        return;
    }

    if (! isPreviewCardEligible (stepIndex, displayedRevision))
        return;

    const auto gestureId = nextPreviewGestureId++;
    if (nextPreviewGestureId == 0)
        nextPreviewGestureId = 1;

    chordscript::PreviewRequest request;
    request.mode = chordscript::PreviewMode::held;
    request.stepIndex = stepIndex;
    request.gestureId = gestureId;
    request.cancellationEpoch = epoch;
    request.dataRevision = previewCacheRevision;
    request.pitches = previewPitchMasks[static_cast<std::size_t> (stepIndex)];
    request.velocity = previewVelocity;
    publishPreviewRequest (request);
}

chordscript::PreviewFeedback ChordTextAudioProcessor::getPreviewFeedback()
{
    const auto epoch = previewCancellationEpoch.load (std::memory_order_acquire);
    const auto revision = previewDataRevision.load (std::memory_order_acquire);

    chordscript::PreviewFeedback incoming;
    if (previewExchange.readLatestFeedback (incoming)
        && incoming.cancellationEpoch == epoch
        && incoming.dataRevision == revision)
    {
        cachedPreviewFeedback = incoming;
    }

    if (cachedPreviewFeedback.cancellationEpoch != epoch
        || cachedPreviewFeedback.dataRevision != revision)
    {
        cachedPreviewFeedback = {};
        cachedPreviewFeedback.cancellationEpoch = epoch;
        cachedPreviewFeedback.dataRevision = revision;
    }

    return cachedPreviewFeedback;
}

void ChordTextAudioProcessor::cancelChordPreview() noexcept
{
    previewCancellationEpoch.fetch_add (1, std::memory_order_acq_rel);
}

void ChordTextAudioProcessor::setPreviewInputPending (bool pending)
{
    if (previewInputPending == pending)
        return;

    previewInputPending = pending;

    if (pending)
        cancelChordPreview();
}

void ChordTextAudioProcessor::publishPreviewFeedback (
    const chordscript::PreviewRequest& request)
{
    previewExchange.publishFeedback (chordscript::makePreviewFeedback (request));
}

void ChordTextAudioProcessor::rebuildPlan()
{
    // Every rebuild invalidates the old preview snapshot. The revision capture
    // below keeps a cache published by an older rebuild from becoming eligible.
    const auto revision = previewDataRevision.fetch_add (1, std::memory_order_acq_rel) + 1;
    previewCacheRevision = 0;
    previewPitchMasks.clear();
    previewVelocity = 100;
    planRebuildPending.store (false, std::memory_order_release);
    cancelChordPreview();

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

    // Only a coherent rebuild may refresh the preview cache.
    if (previewDataRevision.load (std::memory_order_acquire) == revision)
    {
        previewPitchMasks.assign (voicings.size(), std::array<std::uint64_t, 2> {});
        for (std::size_t index = 0; index < voicings.size(); ++index)
        {
            for (const auto pitch : voicings[index])
            {
                const auto clamped = std::clamp (pitch, 0, 127);
                previewPitchMasks[index][static_cast<std::size_t> (clamped / 64)]
                    |= std::uint64_t { 1 } << (clamped % 64);
            }
        }
        previewVelocity = velocity;
        previewCacheRevision = revision;
    }

    learningRows.clear();
    learningRows.reserve (progression.steps.size());

    for (std::size_t index = 0; index < progression.steps.size(); ++index)
    {
        const auto& step = progression.steps[index];
        LearningRow row;
        row.chord = step.chord.symbol();
        row.degree = chordtext::degreeLabel (step.chord, key);
        row.notes = chordtext::notesForVoicing (step.chord, voicings[index]);
        row.durationBeats = step.durationBeats;
        row.numericDegree = step.numericDegree;
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

    chordtext::MidiFileSettings settings;
    const auto cachedBpm = lastHostBpm.load (std::memory_order_relaxed);
    settings.bpm = cachedBpm > 0.0 && std::isfinite (cachedBpm) ? cachedBpm : 120.0;
    settings.includeTempoAndTimeSignature = mode == MidiFileMode::exportFile;

    std::string error;
    if (! chordtext::writeMidiFile (
            exportPlan,
            std::filesystem::u8path (file.getFullPathName().toStdString()),
            settings,
            error))
    {
        errorMessage = juce::String::fromUTF8 (error.c_str());
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
    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                            .getChildFile ("ChordText")
                            .getChildFile ("Clips");
    return createDragClipFile (errorMessage, folder);
}

juce::File ChordTextAudioProcessor::createDragClipFile (juce::String& errorMessage,
                                                        const juce::File& folder)
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

    // Cancel after caching releases so the next block can still silence the
    // notes that were sounding before the reset.
    cancelChordPreview();
    audioPreviewRequest = {};
    appliedPreviewRequest = {};
    bypassInterval = false;
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

    cancelChordPreview();
    audioPreviewRequest = {};
    appliedPreviewRequest = {};
    bypassInterval = false;
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

    // The first normal block after a bypassed interval invalidates anything
    // requested while bypassed. A fresh gesture is required afterwards.
    if (bypassInterval)
    {
        bypassInterval = false;
        cancelChordPreview();
        audioPreviewRequest = {};
        appliedPreviewRequest = {};
    }

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
        // A preview request can arrive before its rebuilt plan is consumed.
        // Preserve manual notes here; epoch/revision validation below decides
        // whether that gesture should continue, be replaced, or be released.
        if (appliedPreviewRequest.mode == chordscript::PreviewMode::idle)
            releaseActiveNotes (midi, 0);
        hasExpectedBeat = false;
    }

    // The latest requested preview state wins. Consume at most one publication
    // per block; no publication means the previously requested state stands
    // until cancellation or an idle request replaces it.
    {
        chordscript::PreviewRequest request;
        if (previewExchange.readLatestRequest (request))
            audioPreviewRequest = request;
    }

    auto disableLiveGeneration = [this, &midi]
    {
        releaseActiveNotes (midi, 0);
        expectedNextBeat = 0.0;
        hasExpectedBeat = false;
        transportAvailable.store (false, std::memory_order_relaxed);
    };

    auto* hostPlayHead = getPlayHead();
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (hostPlayHead != nullptr)
        position = hostPlayHead->getPosition();

    playheadAvailable.store (position.hasValue(), std::memory_order_relaxed);

    juce::Optional<double> ppqPosition;
    juce::Optional<double> bpm;
    if (position.hasValue())
    {
        ppqPosition = position->getPpqPosition();
        bpm = position->getBpm();

        // Cache the tempo while the playhead is legally readable. Export runs
        // on the message thread and must not call getPosition() itself.
        if (bpm && std::isfinite (*bpm) && *bpm > 0.0)
            lastHostBpm.store (*bpm, std::memory_order_relaxed);
    }

    const auto transportValid = position.hasValue()
                                && position->getIsPlaying()
                                && ppqPosition.hasValue()
                                && bpm.hasValue()
                                && std::isfinite (*ppqPosition)
                                && std::isfinite (*bpm)
                                && *bpm > 0.0;
    transportAvailable.store (transportValid, std::memory_order_relaxed);

    const auto cancellationEpoch = previewCancellationEpoch.load (std::memory_order_acquire);
    const auto dataRevision = previewDataRevision.load (std::memory_order_acquire);

    auto desiredPreview = audioPreviewRequest;
    if (desiredPreview.mode != chordscript::PreviewMode::idle
        && (desiredPreview.cancellationEpoch != cancellationEpoch
            || desiredPreview.dataRevision != dataRevision))
    {
        desiredPreview = makeIdlePreviewRequest();
        desiredPreview.cancellationEpoch = cancellationEpoch;
        desiredPreview.dataRevision = dataRevision;
    }

    const auto wasPreviewing = appliedPreviewRequest.mode != chordscript::PreviewMode::idle;
    if (desiredPreview.mode != chordscript::PreviewMode::idle)
    {
        if (! wasPreviewing
            || appliedPreviewRequest.gestureId != desiredPreview.gestureId
            || appliedPreviewRequest.pitches != desiredPreview.pitches
            || appliedPreviewRequest.velocity != desiredPreview.velocity)
        {
            // Manual output is exclusive: release everything first, including
            // pitches shared with the chord being started.
            releaseActiveNotes (midi, 0);

            for (auto pitchIndex = 0; pitchIndex < 128; ++pitchIndex)
            {
                const auto word = static_cast<std::size_t> (pitchIndex / 64);
                const auto bit = std::uint64_t { 1 } << (pitchIndex % 64);
                if ((desiredPreview.pitches[word] & bit) == 0)
                    continue;

                midi.addEvent (juce::MidiMessage::noteOn (
                                   1, pitchIndex,
                                   static_cast<juce::uint8> (desiredPreview.velocity)),
                               0);
                activePitches[static_cast<std::size_t> (pitchIndex)] = true;
            }

            appliedPreviewRequest = desiredPreview;
            expectedNextBeat = 0.0;
            hasExpectedBeat = false;
        }

        currentStep.store (-1, std::memory_order_relaxed);
        publishPreviewFeedback (desiredPreview);
        return;
    }

    if (wasPreviewing)
    {
        // Return to the progression at the current host position.
        releaseActiveNotes (midi, 0);
        appliedPreviewRequest = {};
        expectedNextBeat = 0.0;
        hasExpectedBeat = false;
        publishPreviewFeedback (desiredPreview);
        // Fall through to the normal scheduler in this block.
    }

    if (! position.hasValue())
    {
        disableLiveGeneration();
        return;
    }

    if (! transportValid)
    {
        disableLiveGeneration();
        return;
    }

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
    currentStep.store (-1, std::memory_order_relaxed);

    // Bypassed output never starts a note. Mark the interval so a request made
    // while bypassed cannot revive on the first normal block.
    bypassInterval = true;
    cancelChordPreview();
    audioPreviewRequest = {};
    appliedPreviewRequest = {};

    chordscript::PreviewRequest idle;
    idle.cancellationEpoch = previewCancellationEpoch.load (std::memory_order_acquire);
    idle.dataRevision = previewDataRevision.load (std::memory_order_acquire);
    publishPreviewFeedback (idle);
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
    const auto selected = effectiveScaleIndex.load (std::memory_order_relaxed);
    state.getChildWithProperty ("id", "scaleMode").setProperty ("value", selected, nullptr);
    if (selected <= 1)
        state.getChildWithProperty ("id", "mode").setProperty ("value", selected, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChordTextAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    auto scaleState = tree.getChildWithProperty ("id", "scaleMode");
    if (! scaleState.isValid())
    {
        const auto legacy = tree.getChildWithProperty ("id", "mode");
        scaleState = juce::ValueTree ("PARAM");
        scaleState.setProperty ("id", "scaleMode", nullptr);
        scaleState.setProperty ("value", std::clamp (static_cast<int> (legacy.getProperty ("value", 0)), 0, 1), nullptr);
        tree.addChild (scaleState, -1, nullptr);
    }
    const auto selected = std::clamp (static_cast<int> (scaleState.getProperty ("value", 0)), 0, 8);
    scaleState.setProperty ("value", selected, nullptr);

    // Invalidate preview before any deferred message-thread rebuild. The
    // revision advance rejects the old pitch snapshot immediately.
    planRebuildPending.store (true, std::memory_order_release);
    previewDataRevision.fetch_add (1, std::memory_order_acq_rel);
    cancelChordPreview();

    // The plan rebuild owns message-thread-only state. Restoring state from a
    // host is expected on the message thread; if a host breaks that contract,
    // marshal the rebuild through the normal async path instead of touching
    // the plan directly.
    const auto onMessageThread = juce::MessageManager::existsAndIsCurrentThread();

    cancelPendingUpdate();
    {
        const juce::ScopedValueSetter<const ChordTextAudioProcessor*> guard (synchronizingProcessor, this);
        apvts.replaceState (tree);
        effectiveScaleIndex.store (selected, std::memory_order_relaxed);
    }
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
