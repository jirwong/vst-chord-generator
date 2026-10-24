#include "ChordPreviewChecks.h"
#include "Check.h"
#include "Plugin/ChordPreview.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <utility>
#include <vector>

struct ChordPreviewTestAccess
{
    static bool deferPendingPlan (ChordTextAudioProcessor& processor)
    {
        const auto previous = processor.sharedPlanIndex.fetch_and (
            ~processor.freshPlanBit, std::memory_order_acq_rel);
        return (previous & processor.freshPlanBit) != 0;
    }

    static void deliverPendingPlan (ChordTextAudioProcessor& processor)
    {
        processor.sharedPlanIndex.fetch_or (processor.freshPlanBit, std::memory_order_release);
    }
};

namespace
{
class PreviewPlayHead : public juce::AudioPlayHead
{
public:
    void setPosition (double ppqIn, bool playingIn, double bpmIn)
    {
        hasPosition = true;
        hasPpq = true;
        hasBpm = true;
        ppq = ppqIn;
        playing = playingIn;
        bpm = bpmIn;
    }

    void setNoPosition() { hasPosition = false; }
    void setMissingPpq() { hasPpq = false; }
    void setMissingBpm() { hasBpm = false; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! hasPosition)
            return {};

        PositionInfo info;
        if (hasPpq)
            info.setPpqPosition (ppq);
        if (hasBpm)
            info.setBpm (bpm);
        info.setIsPlaying (playing);
        return info;
    }

private:
    double ppq = 0.0;
    bool playing = true;
    double bpm = 120.0;
    bool hasPosition = true;
    bool hasPpq = true;
    bool hasBpm = true;
};

juce::MidiBuffer renderBlock (ChordTextAudioProcessor& processor, int samples = 512)
{
    juce::AudioBuffer<float> audio (2, samples);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    return midi;
}

juce::MouseEvent makeMouseEvent (juce::Component& target, juce::Point<float> position,
                                 juce::ModifierKeys modifiers)
{
    return juce::MouseEvent (
        juce::Desktop::getInstance().getMainMouseSource(),
        position, modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &target, &target, juce::Time::getCurrentTime(),
        juce::Point<float> (0.0f, 0.0f), juce::Time::getCurrentTime(),
        1, true);
}

juce::Component* findEditorChild (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id)
        return &parent;

    for (auto* child : parent.getChildren())
        if (auto* match = findEditorChild (*child, id))
            return match;

    return nullptr;
}

std::vector<int> readMidiNoteOnPitches (const juce::File& file)
{
    std::vector<int> result;
    juce::FileInputStream input (file);
    if (! input.openedOk())
        return result;

    juce::MidiFile midiFile;
    if (! midiFile.readFrom (input))
        return result;

    for (int track = 0; track < midiFile.getNumTracks(); ++track)
    {
        const auto* sequence = midiFile.getTrack (track);
        for (int index = 0; index < sequence->getNumEvents(); ++index)
        {
            const auto message = sequence->getEventPointer (index)->message;
            if (message.isNoteOn())
                result.push_back (message.getNoteNumber());
        }
    }

    std::sort (result.begin(), result.end());
    return result;
}

bool midiContainsTempoAndTimeSignature (const juce::File& file)
{
    juce::FileInputStream input (file);
    if (! input.openedOk())
        return false;

    juce::MidiFile midiFile;
    if (! midiFile.readFrom (input))
        return false;

    auto hasTempo = false;
    auto hasTimeSignature = false;
    for (int track = 0; track < midiFile.getNumTracks(); ++track)
        for (int index = 0; index < midiFile.getTrack (track)->getNumEvents(); ++index)
        {
            const auto message = midiFile.getTrack (track)->getEventPointer (index)->message;
            hasTempo = hasTempo || message.isTempoMetaEvent();
            hasTimeSignature = hasTimeSignature || message.isTimeSignatureMetaEvent();
        }
    return hasTempo && hasTimeSignature;
}

void captureEditor (juce::Component& editor, const juce::String& name)
{
    const auto directoryPath = juce::SystemStats::getEnvironmentVariable (
        "CHORDSCRIPT_UI_SNAPSHOT_DIR", {});
    if (directoryPath.isEmpty())
        return;

    juce::File directory (directoryPath);
    CHECK (directory.createDirectory().wasOk());
    juce::FileOutputStream output (directory.getChildFile (name));
    CHECK (output.openedOk());
    output.setPosition (0);
    output.truncate();
    CHECK (juce::PNGImageFormat().writeImageToStream (
        editor.createComponentSnapshot (editor.getLocalBounds()), output));
}

std::vector<int> pitches (const juce::MidiBuffer& midi, bool noteOn)
{
    std::vector<int> result;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn() == noteOn)
            result.push_back (metadata.getMessage().getNoteNumber());
    return result;
}

std::vector<int> sortedUnique (std::vector<int> values)
{
    std::sort (values.begin(), values.end());
    values.erase (std::unique (values.begin(), values.end()), values.end());
    return values;
}

bool noteOffsPrecedeNoteOns (const juce::MidiBuffer& midi)
{
    auto seenNoteOn = false;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            seenNoteOn = true;
        else if (message.isNoteOff() && seenNoteOn)
            return false;
    }

    return true;
}

void configureRootPosition (ChordTextAudioProcessor& processor, const char* text,
                            int velocity = 100)
{
    if (auto* voicing = processor.apvts.getParameter ("voicing"))
        voicing->setValueNotifyingHost (1.0f);

    if (auto* parameter = dynamic_cast<juce::AudioParameterInt*> (
            processor.apvts.getParameter ("velocity")))
    {
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (static_cast<float> (velocity)));
    }

    processor.setProgressionText (text);
    processor.rebuildPlan();
}

void fillRequest (int step, chordscript::PreviewRequest& request)
{
    request.mode = step % 3 == 0 ? chordscript::PreviewMode::idle
                  : step % 3 == 1 ? chordscript::PreviewMode::momentary
                                  : chordscript::PreviewMode::held;
    request.stepIndex = step;
    request.gestureId = 0x0123456789abcdefull + static_cast<std::uint64_t> (step);
    request.cancellationEpoch = 0x100000000ull + static_cast<std::uint64_t> (step);
    request.dataRevision = 0x200000000ull + static_cast<std::uint64_t> (step) * 7u;
    request.pitches = { 0x9e3779b97f4a7c15ull * static_cast<std::uint64_t> (step + 1),
                        ~static_cast<std::uint64_t> (step) };
    request.velocity = 1 + step % 127;
}

bool sameUnlessPitch (const chordscript::PreviewRequest& a,
                      const chordscript::PreviewRequest& b)
{
    return a.mode == b.mode
        && a.stepIndex == b.stepIndex
        && a.gestureId == b.gestureId
        && a.cancellationEpoch == b.cancellationEpoch
        && a.dataRevision == b.dataRevision
        && a.pitches == b.pitches
        && a.velocity == b.velocity;
}

void checkPreviewExchange()
{
    chordscript::ChordPreviewExchange exchange;
    chordscript::PreviewRequest initial;
    initial.gestureId = 77;
    CHECK (! exchange.readLatestRequest (initial));
    CHECK (initial.gestureId == 77); // failed read leaves cached state intact

    chordscript::PreviewRequest first;
    fillRequest (1, first);
    exchange.publishRequest (first);

    chordscript::PreviewRequest second;
    fillRequest (2, second);
    exchange.publishRequest (second);

    chordscript::PreviewRequest received;
    CHECK (exchange.readLatestRequest (received));
    CHECK (sameUnlessPitch (received, second));
    CHECK (received.pitches == second.pitches);

    chordscript::PreviewRequest untouched = received;
    CHECK (! exchange.readLatestRequest (received));
    CHECK (sameUnlessPitch (received, untouched));

    chordscript::PreviewFeedback feedback;
    feedback.mode = chordscript::PreviewMode::held;
    feedback.stepIndex = 4;
    feedback.gestureId = 0xfedcba9876543210ull;
    feedback.cancellationEpoch = 0x1111111111111111ull;
    feedback.dataRevision = 0x2222222222222222ull;
    exchange.publishFeedback (feedback);

    chordscript::PreviewFeedback feedbackLater = feedback;
    feedbackLater.mode = chordscript::PreviewMode::momentary;
    feedbackLater.gestureId = 0x3333333333333333ull;
    exchange.publishFeedback (feedbackLater);

    chordscript::PreviewFeedback feedbackReceived;
    CHECK (exchange.readLatestFeedback (feedbackReceived));
    CHECK (feedbackReceived.mode == feedbackLater.mode);
    CHECK (feedbackReceived.stepIndex == feedbackLater.stepIndex);
    CHECK (feedbackReceived.gestureId == feedbackLater.gestureId);
    CHECK (feedbackReceived.cancellationEpoch == feedbackLater.cancellationEpoch);
    CHECK (feedbackReceived.dataRevision == feedbackLater.dataRevision);

    chordscript::PreviewFeedback feedbackUntouched = feedbackReceived;
    CHECK (! exchange.readLatestFeedback (feedbackReceived));
    CHECK (feedbackReceived.gestureId == feedbackUntouched.gestureId);

    // Feedback copies the applied request's identity, not the live processor
    // atomics, so cancellation cannot stamp an old gesture as current.
    chordscript::PreviewRequest applied;
    fillRequest (1, applied);
    const auto mapped = chordscript::makePreviewFeedback (applied);
    CHECK (mapped.mode == applied.mode);
    CHECK (mapped.stepIndex == applied.stepIndex);
    CHECK (mapped.gestureId == applied.gestureId);
    CHECK (mapped.cancellationEpoch == applied.cancellationEpoch);
    CHECK (mapped.dataRevision == applied.dataRevision);
}

void checkPreviewExchangeStress()
{
    chordscript::ChordPreviewExchange exchange;
    constexpr int publicationCount = 20000;

    std::atomic<bool> producerDone { false };
    std::thread producer ([&exchange, &producerDone, publicationCount]
    {
        for (int step = 1; step <= publicationCount; ++step)
        {
            chordscript::PreviewRequest request;
            fillRequest (step, request);
            exchange.publishRequest (request);
        }
        producerDone.store (true, std::memory_order_release);
    });

    chordscript::PreviewRequest received;
    chordscript::PreviewGestureId lastSeen = 0;
    int readCount = 0;

    auto consume = [&]
    {
        while (exchange.readLatestRequest (received))
        {
            chordscript::PreviewRequest expected;
            fillRequest (received.stepIndex, expected);
            CHECK (sameUnlessPitch (received, expected));
            lastSeen = received.gestureId;
            ++readCount;
        }
    };

    while (! producerDone.load (std::memory_order_acquire))
        consume();

    producer.join();
    consume();

    chordscript::PreviewRequest final;
    fillRequest (publicationCount, final);
    CHECK (lastSeen == final.gestureId);
    CHECK (readCount > 0);
}
void checkMomentaryAndHeldPreview()
{
    // Preview works without a playhead while the host keeps processing blocks.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C", 83);
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        const auto revision = processor.getPreviewCardRevision();
        CHECK (revision != 0);
        const auto gesture = processor.beginMomentaryPreview (0, revision);
        CHECK (gesture != 0);

        const auto started = renderBlock (processor);
        CHECK ((pitches (started, true) == std::vector<int> {48, 52, 55}));
        for (const auto metadata : started)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                CHECK (message.getChannel() == 1);
                CHECK (message.getVelocity() == 83);
                CHECK (metadata.samplePosition == 0);
            }
        }

        CHECK (renderBlock (processor).isEmpty());

        const auto feedback = processor.getPreviewFeedback();
        CHECK (feedback.mode == chordscript::PreviewMode::momentary);
        CHECK (feedback.stepIndex == 0);
        CHECK (feedback.gestureId == gesture);

        processor.endMomentaryPreview (gesture);
        const auto released = renderBlock (processor);
        CHECK (pitches (released, true).empty());
        CHECK ((sortedUnique (pitches (released, false)) == std::vector<int> {48, 52, 55}));
        CHECK (renderBlock (processor).isEmpty());
    }

    // A stopped transport does not block or repeatedly release a right hold.
    {
        PreviewPlayHead head;
        head.setPosition (0.0, false, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 512);

        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);

        processor.toggleHeldPreview (0, revision);
        const auto released = renderBlock (processor);
        CHECK (pitches (released, true).empty());
        CHECK ((sortedUnique (pitches (released, false)) == std::vector<int> {48, 52, 55}));
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // Bass is present in momentary and held output and is released on automation.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "[C5,E4,G4]:2", 83);
        auto* bass = processor.apvts.getParameter ("bassOctaves");
        CHECK (bass != nullptr);
        if (bass != nullptr)
            bass->setValueNotifyingHost (1.0f / 3.0f);
        processor.rebuildPlan();
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        const auto outputDirectory = juce::File::getCurrentWorkingDirectory()
                                         .getChildFile ("build/Tests");
        const auto exportFile = outputDirectory.getNonexistentChildFile (
            "custom-bass-export", ".mid", false);
        juce::String error;
        CHECK (processor.exportMidiFile (exportFile, error));
        CHECK ((readMidiNoteOnPitches (exportFile) == std::vector<int> {60, 64, 67, 72}));
        CHECK (midiContainsTempoAndTimeSignature (exportFile));
        const auto dragFile = processor.createDragClipFile (error, outputDirectory);
        CHECK (dragFile.existsAsFile());
        CHECK ((readMidiNoteOnPitches (dragFile) == std::vector<int> {60, 64, 67, 72}));
        CHECK (! midiContainsTempoAndTimeSignature (dragFile));

        auto revision = processor.getPreviewCardRevision();
        auto gesture = processor.beginMomentaryPreview (0, revision);
        CHECK (gesture != 0);
        CHECK ((sortedUnique (pitches (renderBlock (processor), true))
                == std::vector<int> {60, 64, 67, 72}));
        processor.endMomentaryPreview (gesture);
        CHECK ((sortedUnique (pitches (renderBlock (processor), false))
                == std::vector<int> {60, 64, 67, 72}));

        revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((sortedUnique (pitches (renderBlock (processor), true))
                == std::vector<int> {60, 64, 67, 72}));
        if (bass != nullptr)
            bass->setValueNotifyingHost (2.0f / 3.0f);
        CHECK (processor.getPreviewCardRevision() == 0);
        CHECK (processor.beginMomentaryPreview (0, revision) == 0);
        CHECK ((sortedUnique (pitches (renderBlock (processor), false))
                == std::vector<int> {60, 64, 67, 72}));
        CHECK (pitches (renderBlock (processor), true).empty());
    }
}

void checkPreviewReplacementAndCoalescing()
{
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        processor.beginMomentaryPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        processor.beginMomentaryPreview (1, revision);
        auto replacement = renderBlock (processor);
        CHECK ((sortedUnique (pitches (replacement, false)) == std::vector<int> {48, 52, 55}));
        CHECK ((pitches (replacement, true) == std::vector<int> {50, 53, 57}));
        CHECK (noteOffsPrecedeNoteOns (replacement));

        // Selecting the same chord again releases and retriggers it.
        processor.beginMomentaryPreview (0, revision);
        replacement = renderBlock (processor);
        CHECK ((sortedUnique (pitches (replacement, false)) == std::vector<int> {50, 53, 57}));
        CHECK ((pitches (replacement, true) == std::vector<int> {48, 52, 55}));
        CHECK (noteOffsPrecedeNoteOns (replacement));

        // A momentary preview replaces a held chord and release does not restore it.
        processor.toggleHeldPreview (1, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {50, 53, 57}));
        const auto momentary = processor.beginMomentaryPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
        processor.endMomentaryPreview (momentary);
        const auto afterRelease = renderBlock (processor);
        CHECK (pitches (afterRelease, true).empty());
        CHECK ((sortedUnique (pitches (afterRelease, false)) == std::vector<int> {48, 52, 55}));
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // A stale momentary release cannot cancel a newer hold.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        const auto stale = processor.beginMomentaryPreview (0, revision);
        CHECK (stale != 0);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        processor.toggleHeldPreview (1, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {50, 53, 57}));
        processor.endMomentaryPreview (stale);
        CHECK (renderBlock (processor).isEmpty());
        const auto feedback = processor.getPreviewFeedback();
        CHECK (feedback.mode == chordscript::PreviewMode::held);
        CHECK (feedback.stepIndex == 1);
    }

    // Down and up before the next block produce no sound.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        const auto gesture = processor.beginMomentaryPreview (0, revision);
        CHECK (gesture != 0);
        processor.endMomentaryPreview (gesture);
        CHECK (renderBlock (processor).isEmpty());
    }

    // Two replacements before one block coalesce to the final chord.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        processor.beginMomentaryPreview (0, revision);
        processor.beginMomentaryPreview (1, revision);
        const auto midi = renderBlock (processor);
        CHECK (pitches (midi, false).empty());
        CHECK ((pitches (midi, true) == std::vector<int> {50, 53, 57}));
    }

    // Shared pitches are released before the replacement chord starts.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, C7");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        processor.beginMomentaryPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
        processor.beginMomentaryPreview (1, revision);
        const auto midi = renderBlock (processor);
        CHECK ((sortedUnique (pitches (midi, false)) == std::vector<int> {48, 52, 55}));
        CHECK ((pitches (midi, true) == std::vector<int> {48, 52, 55, 58}));
        CHECK (noteOffsPrecedeNoteOns (midi));
    }

    // Invalid cards and stale displayed revisions are rejected.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        CHECK (processor.beginMomentaryPreview (-1, revision) == 0);
        CHECK (processor.beginMomentaryPreview (1, revision) == 0);
        processor.toggleHeldPreview (2, revision);
        CHECK (renderBlock (processor).isEmpty());

        processor.setProgressionText ("Dm");
        CHECK (processor.getPreviewCardRevision() != revision);
        CHECK (processor.beginMomentaryPreview (0, revision) == 0);
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }
}

void checkPreviewPlaybackPriority()
{
    constexpr int blockSize = 512;
    constexpr double blockBeats = blockSize * 120.0 / (60.0 * 48000.0);

    // A held chord overrides the progression across chord boundaries.
    {
        PreviewPlayHead head;
        head.setPosition (0.0, true, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm, G");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, blockSize);

        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (2, revision);

        auto replaced = renderBlock (processor);
        CHECK ((sortedUnique (pitches (replaced, false)) == std::vector<int> {48, 52, 55}));
        CHECK ((pitches (replaced, true) == std::vector<int> {55, 59, 62}));
        CHECK (noteOffsPrecedeNoteOns (replaced));

        auto playedDuringHold = false;
        for (int block = 1; block <= 234; ++block)
        {
            head.setPosition (block * blockBeats, true, 120.0);
            if (! renderBlock (processor).isEmpty())
                playedDuringHold = true;
        }
        CHECK (! playedDuringHold);

        head.setPosition (5.0, true, 120.0);
        processor.toggleHeldPreview (2, revision);
        const auto resumed = renderBlock (processor);
        CHECK ((sortedUnique (pitches (resumed, false)) == std::vector<int> {55, 59, 62}));
        CHECK ((pitches (resumed, true) == std::vector<int> {50, 53, 57}));
        CHECK (noteOffsPrecedeNoteOns (resumed));
    }

    // Releasing exactly at a boundary emits the new chord once.
    {
        PreviewPlayHead head;
        head.setPosition (0.0, true, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm, G");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, blockSize);

        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (2, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {55, 59, 62}));

        head.setPosition (4.0, true, 120.0);
        processor.toggleHeldPreview (2, revision);
        const auto resumed = renderBlock (processor);
        const auto offPitches = pitches (resumed, false);
        for (const auto expected : {55, 59, 62})
            CHECK (std::find (offPitches.begin(), offPitches.end(), expected) != offPitches.end());
        CHECK ((pitches (resumed, true) == std::vector<int> {50, 53, 57}));
        CHECK (noteOffsPrecedeNoteOns (resumed));
    }

    // A release just before a boundary resumes mid-chord and crosses it once.
    {
        PreviewPlayHead head;
        head.setPosition (0.0, true, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm, G");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 480);

        CHECK ((pitches (renderBlock (processor, 480), true) == std::vector<int> {48, 52, 55}));
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (2, revision);
        CHECK ((pitches (renderBlock (processor, 480), true) == std::vector<int> {55, 59, 62}));

        head.setPosition (3.99, true, 120.0);
        processor.toggleHeldPreview (2, revision);
        const auto resumed = renderBlock (processor, 480);

        int cOnsAtZero = 0;
        int cOffsAt240 = 0;
        int dmOnsAt240 = 0;
        int otherNoteOns = 0;
        int lastCOffIndex = -1;
        int firstDmOnIndex = -1;
        int index = 0;

        for (const auto metadata : resumed)
        {
            const auto message = metadata.getMessage();
            const auto pitch = message.getNoteNumber();
            const auto isC = pitch == 48 || pitch == 52 || pitch == 55;
            const auto isDm = pitch == 50 || pitch == 53 || pitch == 57;

            if (message.isNoteOn() && metadata.samplePosition == 0 && isC)
                ++cOnsAtZero;
            else if (message.isNoteOn() && metadata.samplePosition == 240 && isDm)
                ++dmOnsAt240;
            else if (message.isNoteOn())
                ++otherNoteOns;

            if (message.isNoteOff() && metadata.samplePosition == 240 && isC)
            {
                ++cOffsAt240;
                lastCOffIndex = index;
            }

            if (message.isNoteOn() && metadata.samplePosition == 240 && isDm
                && firstDmOnIndex < 0)
                firstDmOnIndex = index;

            ++index;
        }

        CHECK (cOnsAtZero == 3);
        CHECK (cOffsAt240 == 3);
        CHECK (dmOnsAt240 == 3);
        CHECK (otherNoteOns == 0);
        CHECK (lastCOffIndex >= 0);
        CHECK (firstDmOnIndex >= 0);
        CHECK (lastCOffIndex < firstDmOnIndex);
    }
}

void checkPreviewBeforeRebuiltPlanDelivery()
{
    for (const auto mode : {chordscript::PreviewMode::momentary, chordscript::PreviewMode::held})
    {
        PreviewPlayHead head;
        head.setPosition (0.0, true, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 512);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        processor.setProgressionText ("C, Dm, G");
        const auto revision = processor.getPreviewCardRevision();
        CHECK (revision != 0);

        // Model a rebuild/request arriving after this block checked the plan
        // mailbox but before it consumed the preview mailbox. Only the fresh
        // flag is deferred: slot ownership and all payloads remain unchanged.
        CHECK (ChordPreviewTestAccess::deferPendingPlan (processor));
        chordscript::PreviewGestureId gesture = 0;
        if (mode == chordscript::PreviewMode::momentary)
        {
            gesture = processor.beginMomentaryPreview (2, revision);
            CHECK (gesture != 0);
        }
        else
        {
            processor.toggleHeldPreview (2, revision);
        }

        const auto started = renderBlock (processor);
        CHECK ((pitches (started, false) == std::vector<int> {48, 52, 55}));
        CHECK ((pitches (started, true) == std::vector<int> {55, 59, 62}));
        CHECK (noteOffsPrecedeNoteOns (started));
        const auto startedFeedback = processor.getPreviewFeedback();
        CHECK (startedFeedback.mode == mode);
        CHECK (startedFeedback.stepIndex == 2);

        ChordPreviewTestAccess::deliverPendingPlan (processor);
        head.setPosition (5.0, true, 120.0);
        // Learning about the new progression must neither release nor
        // retrigger the manual chord already sounding from that revision.
        CHECK (renderBlock (processor).isEmpty());
        const auto heldFeedback = processor.getPreviewFeedback();
        CHECK (heldFeedback.mode == mode);
        CHECK (heldFeedback.gestureId == startedFeedback.gestureId);

        if (mode == chordscript::PreviewMode::momentary)
            processor.endMomentaryPreview (gesture);
        else
            processor.toggleHeldPreview (2, revision);

        const auto resumed = renderBlock (processor);
        CHECK ((pitches (resumed, false) == std::vector<int> {55, 59, 62}));
        CHECK ((pitches (resumed, true) == std::vector<int> {50, 53, 57}));
        CHECK (noteOffsPrecedeNoteOns (resumed));
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }
}

void checkPreviewCancellation()
{
    // Pending text input cancels before the debounce and blocks new gestures.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        processor.setPreviewInputPending (true);
        const auto canceled = renderBlock (processor);
        CHECK (pitches (canceled, true).empty());
        CHECK (! pitches (canceled, false).empty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
        CHECK (processor.beginMomentaryPreview (0, revision) == 0);

        processor.setPreviewInputPending (false);
        CHECK (processor.beginMomentaryPreview (0, revision) != 0);
    }

    // A queued request is invalidated before it reaches an audio block.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();

        processor.beginMomentaryPreview (0, revision);
        processor.setPreviewInputPending (true);
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // Every musical parameter invalidates an active preview. Loop keeps the
    // reusable pitch snapshot; the others also invalidate its revision.
    {
        struct ParameterCase { const char* id; float value; bool rebuildsVoicing; };
        const ParameterCase cases[] {
            {"key", 3.0f / 16.0f, true},
            {"mode", 1.0f, true},
            {"scaleMode", 2.0f / 8.0f, true},
            {"voicing", 0.0f, true},
            {"range", 0.0f, true},
            {"velocity", 0.5f, true},
            {"loop", 0.0f, false}
        };

        for (const auto& parameterCase : cases)
        {
            ChordTextAudioProcessor processor;
            configureRootPosition (processor, "C, Dm");
            processor.setPlayHead (nullptr);
            processor.prepareToPlay (48000.0, 512);
            const auto revision = processor.getPreviewCardRevision();

            processor.toggleHeldPreview (1, revision);
            CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {50, 53, 57}));

            auto* parameter = processor.apvts.getParameter (parameterCase.id);
            CHECK (parameter != nullptr);
            if (parameter == nullptr)
                continue;

            parameter->setValueNotifyingHost (parameterCase.value);
            const auto canceled = renderBlock (processor);
            CHECK (pitches (canceled, true).empty());
            CHECK (! pitches (canceled, false).empty());
            CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

            if (parameterCase.rebuildsVoicing)
            {
                CHECK (processor.getPreviewCardRevision() == 0);
                CHECK (processor.beginMomentaryPreview (1, revision) == 0);
            }
            else
            {
                CHECK (processor.getPreviewCardRevision() == revision);
                CHECK (processor.beginMomentaryPreview (1, revision) != 0);
            }
        }
    }

    // Valid, invalid, and empty progression updates all cancel. Parse failures
    // retain the last valid playback plan but leave preview ineligible.
    {
        const char* updates[] {"Dm", "H", ""};
        for (const auto* text : updates)
        {
            ChordTextAudioProcessor processor;
            configureRootPosition (processor, "C");
            processor.setPlayHead (nullptr);
            processor.prepareToPlay (48000.0, 512);
            const auto revision = processor.getPreviewCardRevision();
            processor.toggleHeldPreview (0, revision);
            CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

            processor.setProgressionText (text);
            const auto canceled = renderBlock (processor);
            CHECK (pitches (canceled, true).empty());
            CHECK (! pitches (canceled, false).empty());
            CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

            if (juce::String (text).isEmpty() || juce::String (text) == "H")
                CHECK (processor.getPreviewCardRevision() == 0);
        }
    }

    // Project restore cancels immediately.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        const auto canceled = renderBlock (processor);
        CHECK (pitches (canceled, true).empty());
        CHECK (! pitches (canceled, false).empty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // Bypass releases output, clears feedback, and invalidates requests made
    // during the bypass interval.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer bypassed;
        processor.processBlockBypassed (audio, bypassed);
        CHECK (pitches (bypassed, true).empty());
        CHECK (! pitches (bypassed, false).empty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

        CHECK (processor.beginMomentaryPreview (0, revision) != 0);
        CHECK (renderBlock (processor).isEmpty());

        CHECK (processor.beginMomentaryPreview (0, revision) != 0);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
    }

    // prepareToPlay queues releases for the next block and cancels preview;
    // releaseResources clears without emitting MIDI.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        processor.prepareToPlay (48000.0, 512);
        const auto afterPrepare = renderBlock (processor);
        CHECK (pitches (afterPrepare, true).empty());
        CHECK ((sortedUnique (pitches (afterPrepare, false)) == std::vector<int> {48, 52, 55}));
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));
        processor.releaseResources();
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
        CHECK (renderBlock (processor).isEmpty());
    }

    // Pending prepareToPlay releases are emitted before a replacement preview.
    {
        PreviewPlayHead head;
        head.setPosition (0.0, true, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, G");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 512);
        CHECK (! pitches (renderBlock (processor), true).empty());

        processor.prepareToPlay (48000.0, 512);
        const auto revision = processor.getPreviewCardRevision();
        processor.beginMomentaryPreview (1, revision);
        const auto midi = renderBlock (processor);
        CHECK (noteOffsPrecedeNoteOns (midi));
        CHECK (! pitches (midi, false).empty());
        CHECK ((pitches (midi, true) == std::vector<int> {55, 59, 62}));
    }
}

void checkPreviewTransportIndependence()
{
    PreviewPlayHead head;
    head.setPosition (0.0, true, 120.0);
    ChordTextAudioProcessor processor;
    configureRootPosition (processor, "C:1, Dm:1, G:2");
    processor.setPlayHead (&head);
    processor.prepareToPlay (48000.0, 512);
    const auto revision = processor.getPreviewCardRevision();
    processor.toggleHeldPreview (2, revision);
    CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {55, 59, 62}));

    const auto expectHeld = [&processor]
    {
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);
    };

    head.setPosition (0.5, false, 120.0);
    expectHeld();
    head.setPosition (1.0, true, 120.0);
    expectHeld();
    head.setPosition (9.0, true, 120.0);
    expectHeld();
    head.setPosition (9.0, true, 60.0);
    expectHeld();
    head.setPosition (3.99, true, 60.0);
    expectHeld();
    head.setPosition (0.01, true, 60.0);
    expectHeld();
    head.setNoPosition();
    expectHeld();
    head.setPosition (0.02, true, 120.0);
    head.setMissingPpq();
    expectHeld();
    head.setPosition (0.03, true, 120.0);
    head.setMissingBpm();
    expectHeld();
    head.setPosition (std::nan (""), true, 120.0);
    expectHeld();
    head.setPosition (0.04, true, 0.0);
    expectHeld();

    // Releasing while normal playback is unavailable goes silent.
    head.setPosition (0.05, false, 120.0);
    processor.toggleHeldPreview (2, revision);
    const auto stoppedRelease = renderBlock (processor);
    CHECK (pitches (stoppedRelease, true).empty());
    CHECK ((sortedUnique (pitches (stoppedRelease, false)) == std::vector<int> {55, 59, 62}));
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

    // Releasing past the end of a non-looping progression goes silent.
    {
        PreviewPlayHead laterHead;
        laterHead.setPosition (6.0, true, 120.0);
        ChordTextAudioProcessor later;
        configureRootPosition (later, "C:1, Dm:1");
        later.apvts.getParameter ("loop")->setValueNotifyingHost (0.0f);
        later.rebuildPlan();
        later.setPlayHead (&laterHead);
        later.prepareToPlay (48000.0, 512);
        const auto laterRevision = later.getPreviewCardRevision();
        later.toggleHeldPreview (0, laterRevision);
        CHECK ((pitches (renderBlock (later), true) == std::vector<int> {48, 52, 55}));
        later.toggleHeldPreview (0, laterRevision);
        const auto ended = renderBlock (later);
        CHECK (pitches (ended, true).empty());
        CHECK ((sortedUnique (pitches (ended, false)) == std::vector<int> {48, 52, 55}));
    }
}

void checkPreviewStateAndFiles()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("build/Tests/chord-preview")
        .getChildFile (juce::Uuid().toString());
    CHECK (directory.createDirectory().wasOk());

    ChordTextAudioProcessor processor;
    configureRootPosition (processor, "C, Dm");
    processor.setPlayHead (nullptr);
    processor.prepareToPlay (48000.0, 512);

    juce::MemoryBlock stateBefore;
    processor.getStateInformation (stateBefore);

    juce::String error;
    const auto exportBefore = directory.getChildFile ("before.mid");
    CHECK (processor.exportMidiFile (exportBefore, error));
    const auto dragBefore = processor.createDragClipFile (error, directory);
    CHECK (error.isEmpty());
    CHECK (dragBefore.existsAsFile());

    juce::MemoryBlock exportBeforeBytes, dragBeforeBytes;
    CHECK (exportBefore.loadFileAsData (exportBeforeBytes));
    CHECK (dragBefore.loadFileAsData (dragBeforeBytes));

    const auto revision = processor.getPreviewCardRevision();
    processor.toggleHeldPreview (0, revision);
    CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

    juce::MemoryBlock stateDuring;
    processor.getStateInformation (stateDuring);
    CHECK (stateBefore == stateDuring);
    if (const auto xml = juce::AudioProcessor::getXmlFromBinary (
            stateDuring.getData(), static_cast<int> (stateDuring.getSize())))
        CHECK (! xml->toString().containsIgnoreCase ("preview"));

    const auto exportDuring = directory.getChildFile ("during.mid");
    CHECK (processor.exportMidiFile (exportDuring, error));
    const auto dragDuring = processor.createDragClipFile (error, directory);
    CHECK (error.isEmpty());
    juce::MemoryBlock exportDuringBytes, dragDuringBytes;
    CHECK (exportDuring.loadFileAsData (exportDuringBytes));
    CHECK (dragDuring.loadFileAsData (dragDuringBytes));
    CHECK (exportBeforeBytes == exportDuringBytes);
    CHECK (dragBeforeBytes == dragDuringBytes);

    // Two instances keep independent requested and applied state.
    {
        ChordTextAudioProcessor first, second;
        configureRootPosition (first, "C, Dm");
        configureRootPosition (second, "C, Dm");
        first.setPlayHead (nullptr);
        second.setPlayHead (nullptr);
        first.prepareToPlay (48000.0, 512);
        second.prepareToPlay (48000.0, 512);

        first.toggleHeldPreview (0, first.getPreviewCardRevision());
        second.toggleHeldPreview (1, second.getPreviewCardRevision());
        CHECK ((pitches (renderBlock (first), true) == std::vector<int> {48, 52, 55}));
        CHECK ((pitches (renderBlock (second), true) == std::vector<int> {50, 53, 57}));

        first.setPreviewInputPending (true);
        CHECK (first.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
        CHECK (second.getPreviewFeedback().mode == chordscript::PreviewMode::held);
        CHECK (renderBlock (second).isEmpty());
        CHECK (second.getPreviewFeedback().mode == chordscript::PreviewMode::held);
    }
}

void checkPreviewCardGestures()
{
    ChordSequenceComponent sequence;
    sequence.setSize (900, 500);
    std::vector<LearningRow> rows (4);
    rows[0].chord = "C";
    rows[1].chord = "Dm";
    rows[2].chord = "G";
    rows[3].chord = "Am";
    sequence.setRows (rows, -1, false, 77);

    auto* content = sequence.getViewedComponent();
    CHECK (content != nullptr);
    if (content == nullptr)
        return;

    for (int index = 0; index < 4; ++index)
        CHECK (sequence.cardAt (sequence.cardBounds (index).getCentre()) == index);

    const auto gapPoint = juce::Point<int> (sequence.cardBounds (0).getRight()
                                                + 6,
                                            sequence.cardBounds (0).getCentreY());
    CHECK (sequence.cardAt (gapPoint) == -1);
    CHECK (sequence.cardAt (juce::Point<int> (10, 10)) == 0);
    CHECK (sequence.cardAt (juce::Point<int> (-10, -10)) == -1);

    int previewCalls = 0;
    int previewIndex = -1;
    std::uint64_t previewRevision = 0;
    std::vector<chordscript::PreviewGestureId> releases;
    chordscript::PreviewGestureId nextId = 100;
    int toggleCalls = 0;
    int toggleIndex = -1;
    std::uint64_t toggleRevision = 0;

    sequence.onMomentaryPreview = [&] (int index, std::uint64_t revision)
    {
        ++previewCalls;
        previewIndex = index;
        previewRevision = revision;
        return nextId++;
    };
    sequence.onMomentaryRelease = [&] (chordscript::PreviewGestureId gestureId)
    {
        releases.push_back (gestureId);
    };
    sequence.onToggleHeldPreview = [&] (int index, std::uint64_t revision)
    {
        ++toggleCalls;
        toggleIndex = index;
        toggleRevision = revision;
    };

    for (int index = 0; index < 4; ++index)
    {
        const auto expectedId = nextId;
        previewCalls = 0;
        content->mouseDown (makeMouseEvent (
            *content, sequence.cardBounds (index).getCentre().toFloat(),
            juce::ModifierKeys::leftButtonModifier));
        CHECK (previewCalls == 1);
        CHECK (previewIndex == index);
        CHECK (previewRevision == 77);
        CHECK (releases.empty());

        // The release is delivered outside the card and still matches the gesture.
        content->mouseUp (makeMouseEvent (
            *content, juce::Point<float> (-50.0f, -50.0f),
            juce::ModifierKeys::leftButtonModifier));
        CHECK (releases.size() == 1);
        if (! releases.empty())
            CHECK (releases.back() == expectedId);
        releases.clear();
        sequence.setRows (rows, -1, false, 77);
    }

    // Right-down toggles once; mouse-up does not toggle again.
    toggleCalls = 0;
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (2).getCentre().toFloat(),
        juce::ModifierKeys::rightButtonModifier));
    CHECK (toggleCalls == 1);
    CHECK (toggleIndex == 2);
    CHECK (toggleRevision == 77);
    content->mouseUp (makeMouseEvent (
        *content, sequence.cardBounds (2).getCentre().toFloat(),
        juce::ModifierKeys::rightButtonModifier));
    CHECK (toggleCalls == 1);

    // Gaps and background do nothing.
    previewCalls = 0;
    toggleCalls = 0;
    content->mouseDown (makeMouseEvent (
        *content, gapPoint.toFloat(), juce::ModifierKeys::leftButtonModifier));
    content->mouseDown (makeMouseEvent (
        *content, juce::Point<float> (10.0f, 300.0f),
        juce::ModifierKeys::rightButtonModifier));
    content->mouseDown (makeMouseEvent (
        *content, juce::Point<float> (10.0f, 300.0f),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (previewCalls == 0);
    CHECK (toggleCalls == 0);

    // Stale, empty, and unversioned rows reject gestures.
    sequence.setRows (rows, -1, true, 77);
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (0).getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier));
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (0).getCentre().toFloat(),
        juce::ModifierKeys::rightButtonModifier));
    CHECK (previewCalls == 0);
    CHECK (toggleCalls == 0);

    sequence.setRows (rows, -1, false, 0);
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (0).getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (previewCalls == 0);

    sequence.setRows ({}, -1, false, 77);
    content->mouseDown (makeMouseEvent (
        *content, juce::Point<float> (10.0f, 10.0f),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (previewCalls == 0);

    // Synthesized capture-loss release carries the matching gesture identity.
    sequence.setRows (rows, -1, false, 77);
    previewCalls = 0;
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (0).getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (previewCalls == 1);
    content->mouseUp (makeMouseEvent (
        *content, juce::Point<float> (-100.0f, -100.0f),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (! releases.empty());

    // A delayed release belongs to its own press, not a newer momentary press.
    sequence.setRows (rows, -1, false, 77);
    releases.clear();
    const auto firstPressId = nextId;
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (0).getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier));
    const auto secondPressId = nextId;
    content->mouseDown (makeMouseEvent (
        *content, sequence.cardBounds (1).getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier));

    content->mouseUp (makeMouseEvent (
        *content, juce::Point<float> (-20.0f, -20.0f),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (releases.size() == 1);
    if (! releases.empty())
        CHECK (releases.back() == firstPressId);

    content->mouseUp (makeMouseEvent (
        *content, juce::Point<float> (-20.0f, -20.0f),
        juce::ModifierKeys::leftButtonModifier));
    CHECK (releases.size() == 2);
    if (releases.size() == 2)
        CHECK (releases.back() == secondPressId);

    // A stale momentary release must not stop a held chord.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        ChordSequenceComponent live;
        live.setSize (900, 500);
        live.setRows (processor.getLearningRows(), -1, false,
                      processor.getPreviewCardRevision());
        auto* liveContent = live.getViewedComponent();
        live.onMomentaryPreview = [&processor] (int index, std::uint64_t revision)
        {
            return processor.beginMomentaryPreview (index, revision);
        };
        live.onMomentaryRelease = [&processor] (chordscript::PreviewGestureId gestureId)
        {
            processor.endMomentaryPreview (gestureId);
        };
        live.onToggleHeldPreview = [&processor] (int index, std::uint64_t revision)
        {
            processor.toggleHeldPreview (index, revision);
        };

        liveContent->mouseDown (makeMouseEvent (
            *liveContent, live.cardBounds (0).getCentre().toFloat(),
            juce::ModifierKeys::leftButtonModifier));
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        liveContent->mouseDown (makeMouseEvent (
            *liveContent, live.cardBounds (1).getCentre().toFloat(),
            juce::ModifierKeys::rightButtonModifier));
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {50, 53, 57}));

        liveContent->mouseUp (makeMouseEvent (
            *liveContent, juce::Point<float> (-50.0f, -50.0f),
            juce::ModifierKeys::leftButtonModifier));
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);
        CHECK (processor.getPreviewFeedback().stepIndex == 1);
    }
}

void checkPreviewScaledAndScrolledCards()
{
    // Normal playback render for visual inspection.
    {
        PreviewPlayHead head;
        head.setPosition (0.0, true, 120.0);
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 512);
        ChordTextAudioProcessorEditor editor (processor, UiSettings { juce::File() });
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        renderBlock (processor);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        captureEditor (editor, "preview-normal.png");
    }

    const auto file = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("build/Tests/ui-settings")
        .getChildFile (juce::Uuid().toString())
        .getChildFile ("preview.json");
    ChordTextAudioProcessor processor;
    configureRootPosition (
        processor,
        "C, Dm, G, Am, C, Dm, G, Am, C, Dm, G, Am, C, Dm, G, Am");
    processor.setPlayHead (nullptr);
    processor.prepareToPlay (48000.0, 512);

    ChordTextAudioProcessorEditor editor (processor, UiSettings (file));
    editor.setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

    auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
    auto* sequence = dynamic_cast<ChordSequenceComponent*> (
        findEditorChild (editor, "chordSequence"));
    auto* hint = dynamic_cast<juce::Label*> (findEditorChild (editor, "previewHint"));
    auto* status = dynamic_cast<juce::Label*> (findEditorChild (editor, "sequenceStatus"));
    CHECK (selector != nullptr);
    CHECK (sequence != nullptr);
    CHECK (hint != nullptr);
    CHECK (status != nullptr);
    if (selector == nullptr || sequence == nullptr || hint == nullptr || status == nullptr)
        return;

    CHECK (hint->getText().contains ("Left press to preview"));
    CHECK (hint->getText().contains ("Right-click to hold"));

    for (const auto percent : {75, 100, 125, 150})
    {
        selector->setSelectedId (percent, juce::sendNotificationSync);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        const auto hintArea = editor.getLocalArea (hint, hint->getLocalBounds());
        const auto statusArea = editor.getLocalArea (status, status->getLocalBounds());
        CHECK (editor.getLocalBounds().contains (hintArea));
        CHECK (editor.getLocalBounds().contains (statusArea));
        CHECK (! hintArea.intersects (statusArea));

        sequence->setViewPosition (0, 90);
        const auto cardIndex = 4;
        const auto cardCentre = sequence->cardBounds (cardIndex).getCentre();
        auto* content = sequence->getViewedComponent();
        const auto editorPoint = editor.getLocalPoint (content, cardCentre.toFloat());
        CHECK (editor.getLocalBounds().contains (editorPoint.toInt()));
        auto* hit = editor.getComponentAt (editorPoint.toInt());
        CHECK (hit == content || (content != nullptr && content->isParentOf (hit)));

        captureEditor (editor, "preview-" + juce::String (percent) + "-idle.png");

        content->mouseDown (makeMouseEvent (
            *content, cardCentre.toFloat(), juce::ModifierKeys::leftButtonModifier));
        renderBlock (processor);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        const auto momentary = processor.getPreviewFeedback();
        CHECK (momentary.mode == chordscript::PreviewMode::momentary);
        CHECK (momentary.stepIndex == cardIndex);
        CHECK (status->getText().contains ("preview"));
        captureEditor (editor, "preview-" + juce::String (percent) + "-momentary.png");

        content->mouseUp (makeMouseEvent (
            *content, juce::Point<float> (-20.0f, -20.0f),
            juce::ModifierKeys::leftButtonModifier));
        renderBlock (processor);

        content->mouseDown (makeMouseEvent (
            *content, cardCentre.toFloat(), juce::ModifierKeys::rightButtonModifier));
        renderBlock (processor);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        const auto held = processor.getPreviewFeedback();
        CHECK (held.mode == chordscript::PreviewMode::held);
        CHECK (held.stepIndex == cardIndex);
        CHECK (status->getText().contains ("held"));
        CHECK (status->getText().contains ("Right-click again"));
        captureEditor (editor, "preview-" + juce::String (percent) + "-held.png");

        content->mouseDown (makeMouseEvent (
            *content, cardCentre.toFloat(), juce::ModifierKeys::rightButtonModifier));
        renderBlock (processor);
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // Zooming during a hold neither cancels nor retriggers the chord.
    selector->setSelectedId (100, juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    sequence->setViewPosition (0, 90);
    auto* content = sequence->getViewedComponent();
    const auto cardCentre = sequence->cardBounds (4).getCentre();
    content->mouseDown (makeMouseEvent (
        *content, cardCentre.toFloat(), juce::ModifierKeys::rightButtonModifier));
    CHECK (! pitches (renderBlock (processor), true).empty());
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);

    selector->setSelectedId (150, juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    CHECK (renderBlock (processor).isEmpty());
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);

    const auto zoomedCardCentre = sequence->cardBounds (4).getCentre();
    const auto zoomedEditorPoint = editor.getLocalPoint (content, zoomedCardCentre.toFloat());
    CHECK (editor.getLocalBounds().contains (zoomedEditorPoint.toInt()));
    content->mouseDown (makeMouseEvent (
        *content, zoomedCardCentre.toFloat(), juce::ModifierKeys::rightButtonModifier));
    const auto released = renderBlock (processor);
    CHECK (pitches (released, true).empty());
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
}

void checkPreviewBadgePadding()
{
    ChordSequenceComponent sequence;
    sequence.setSize (900, 160);
    std::vector<LearningRow> rows (1);
    rows[0].chord = "Cmaj7";
    sequence.setRows (rows, -1, false, 1);
    auto* content = sequence.getViewedComponent();
    CHECK (content != nullptr);
    if (content == nullptr)
        return;

    for (const auto mode : {chordscript::PreviewMode::momentary, chordscript::PreviewMode::held})
    {
        chordscript::PreviewFeedback feedback;
        feedback.mode = mode;
        feedback.stepIndex = 0;
        feedback.gestureId = 1;
        sequence.setPreviewFeedback (feedback);

        for (const auto scale : {0.75f, 1.0f, 1.25f, 1.5f})
        {
            const auto snapshot = content->createComponentSnapshot (
                content->getLocalBounds(), true, scale);
            // The pill ends 17 logical pixels inside the card. Its final
            // four pixels must remain padding, not touch the label's ink.
            const auto card = sequence.cardBounds (0);
            const auto pillRight = card.getRight() - 17;
            const auto pillTop = card.getY() + 17;
            auto paddingHasInk = false;
            auto labelHasInk = false;
            for (auto y = static_cast<int> (std::ceil (pillTop * scale));
                 y < static_cast<int> (std::floor ((pillTop + 16) * scale)); ++y)
            {
                for (auto x = static_cast<int> (std::ceil ((pillRight - 58) * scale));
                     x < static_cast<int> (std::floor (pillRight * scale)); ++x)
                {
                    const auto pixel = snapshot.getPixelAt (x, y);
                    const auto isInk = pixel.getRed() > 65 && pixel.getGreen() > 130
                                       && pixel.getBlue() > 120
                                       && pixel.getGreen() - pixel.getRed() > 35;
                    labelHasInk = labelHasInk || isInk;
                    if (x >= static_cast<int> (std::ceil ((pillRight - 4) * scale)))
                        paddingHasInk = paddingHasInk || isInk;
                }
            }
            CHECK (labelHasInk);
            CHECK (! paddingHasInk);
        }
    }
}

void checkPreviewDisplayedRevision()
{
    const auto file = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("build/Tests/ui-settings")
        .getChildFile (juce::Uuid().toString())
        .getChildFile ("revision.json");
    ChordTextAudioProcessor processor;
    configureRootPosition (processor, "C, Dm");
    processor.setPlayHead (nullptr);
    processor.prepareToPlay (48000.0, 512);

    ChordTextAudioProcessorEditor editor (processor, UiSettings (file));
    editor.setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

    auto* sequence = dynamic_cast<ChordSequenceComponent*> (
        findEditorChild (editor, "chordSequence"));
    CHECK (sequence != nullptr);
    if (sequence == nullptr)
        return;

    auto* content = sequence->getViewedComponent();
    const auto oldCardCentre = sequence->cardBounds (0).getCentre();

    auto waitForDisplayedRevision = [&]
    {
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            if (sequence->getDisplayedRevision() != 0
                && sequence->getDisplayedRevision() == processor.getPreviewCardRevision())
            {
                return;
            }
        }
    };

    // Rebuild before the editor timer refreshes: the old visible card is stale.
    processor.setProgressionText ("G, Am");
    content->mouseDown (makeMouseEvent (
        *content, oldCardCentre.toFloat(), juce::ModifierKeys::leftButtonModifier));
    CHECK (renderBlock (processor).isEmpty());
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

    content->mouseDown (makeMouseEvent (
        *content, oldCardCentre.toFloat(), juce::ModifierKeys::rightButtonModifier));
    CHECK (renderBlock (processor).isEmpty());
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

    // After the timer refresh, the same index plays the newly displayed chord.
    waitForDisplayedRevision();
    CHECK (sequence->getDisplayedRevision() == processor.getPreviewCardRevision());
    const auto newCardCentre = sequence->cardBounds (0).getCentre();
    content->mouseDown (makeMouseEvent (
        *content, newCardCentre.toFloat(), juce::ModifierKeys::leftButtonModifier));
    const auto midi = renderBlock (processor);
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::momentary);
    CHECK (processor.getPreviewFeedback().stepIndex == 0);
    CHECK ((pitches (midi, true) == std::vector<int> {55, 59, 62}));
    content->mouseUp (makeMouseEvent (
        *content, juce::Point<float> (-10.0f, -10.0f),
        juce::ModifierKeys::leftButtonModifier));
    renderBlock (processor);

    // Automation invalidates the displayed revision before its deferred rebuild.
    const auto oldRevision = processor.getPreviewCardRevision();
    processor.apvts.getParameter ("key")->setValueNotifyingHost (3.0f / 16.0f);
    CHECK (processor.getPreviewCardRevision() == 0);
    content->mouseDown (makeMouseEvent (
        *content, newCardCentre.toFloat(), juce::ModifierKeys::leftButtonModifier));
    CHECK (renderBlock (processor).isEmpty());
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    CHECK (oldRevision != 0);

    waitForDisplayedRevision();
    CHECK (sequence->getDisplayedRevision() == processor.getPreviewCardRevision());
    CHECK (processor.getPreviewCardRevision() != 0);
    CHECK (processor.getLearningRows().size() == 2);

    const auto refreshedCentre = sequence->cardBounds (0).getCentre();
    CHECK (sequence->cardAt (refreshedCentre) == 0);

    content->mouseDown (makeMouseEvent (
        *content, refreshedCentre.toFloat(), juce::ModifierKeys::leftButtonModifier));
    const auto automated = renderBlock (processor);
    CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::momentary);
    CHECK (! pitches (automated, true).empty());
    content->mouseUp (makeMouseEvent (
        *content, juce::Point<float> (-10.0f, -10.0f),
        juce::ModifierKeys::leftButtonModifier));
    renderBlock (processor);
}

void checkPreviewEditorCleanup()
{
    // Text changes mark input pending and cancel before the debounce.
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings")
            .getChildFile (juce::Uuid().toString())
            .getChildFile ("cleanup.json");
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        ChordTextAudioProcessorEditor editor (processor, UiSettings (file));
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        auto* text = dynamic_cast<juce::TextEditor*> (
            findEditorChild (editor, "progressionEditor"));
        CHECK (text != nullptr);
        if (text == nullptr)
            return;

        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        text->setText ("Am", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (processor.getPreviewCardRevision() == 0);
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
        CHECK (processor.beginMomentaryPreview (0, revision) == 0);
        CHECK (processor.getProgressionText() == "C, Dm");

        const auto canceled = renderBlock (processor);
        CHECK (pitches (canceled, true).empty());
        CHECK (! pitches (canceled, false).empty());
        captureEditor (editor, "preview-pending.png");

        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        CHECK (processor.getProgressionText() == "Am");
        CHECK (processor.getPreviewCardRevision() != 0);
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);

        text->setText ("H", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        CHECK (! processor.getDiagnostics().empty());
        CHECK (processor.getPreviewCardRevision() == 0);
        captureEditor (editor, "preview-invalid.png");
    }

    // Destruction cancels before flushing the pending edit.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ChordTextAudioProcessorEditor> (
            processor, UiSettings { juce::File() });
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        auto* text = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
        CHECK (text != nullptr);
        if (text == nullptr)
            return;

        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        text->setText ("Dm", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        editor.reset();

        CHECK (processor.getProgressionText() == "Dm");
        const auto afterClose = renderBlock (processor);
        CHECK (pitches (afterClose, true).empty());
        CHECK (! pitches (afterClose, false).empty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // Ordinary focus loss keeps a right hold; a reopened editor starts idle.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ChordTextAudioProcessorEditor> (
            processor, UiSettings { juce::File() });
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK (! pitches (renderBlock (processor), true).empty());

        editor->focusLost (juce::Component::focusChangedByMouseClick);
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);

        processor.toggleHeldPreview (0, revision);
        CHECK (! pitches (renderBlock (processor), false).empty());
        editor.reset();

        ChordTextAudioProcessorEditor reopened (processor, UiSettings { juce::File() });
        reopened.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
        CHECK (renderBlock (processor).isEmpty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }

    // A drag applies a pending rebuild before writing, so it cannot export the
    // old plan; an unchanged coherent drag leaves a hold sounding.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "I");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        ChordTextAudioProcessorEditor editor (processor, UiSettings { juce::File() });
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        auto* clip = dynamic_cast<ClipDragComponent*> (
            findEditorChild (editor, "clipDrag"));
        CHECK (clip != nullptr);
        if (clip == nullptr)
            return;

        processor.apvts.getParameter ("key")->setValueNotifyingHost (3.0f / 16.0f);
        CHECK (processor.isPlanRebuildPending());

        const auto file = clip->createClipFile();
        CHECK (file.existsAsFile());
        CHECK ((readMidiNoteOnPitches (file) == std::vector<int> {50, 54, 57}));
        CHECK (! processor.isPlanRebuildPending());
        file.deleteFile();
    }

    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C, Dm");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        ChordTextAudioProcessorEditor editor (processor, UiSettings { juce::File() });
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        auto* clip = dynamic_cast<ClipDragComponent*> (
            findEditorChild (editor, "clipDrag"));
        CHECK (clip != nullptr);
        if (clip == nullptr)
            return;

        const auto revision = processor.getPreviewCardRevision();
        processor.toggleHeldPreview (0, revision);
        CHECK ((pitches (renderBlock (processor), true) == std::vector<int> {48, 52, 55}));

        const auto file = clip->createClipFile();
        CHECK (file.existsAsFile());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::held);
        CHECK (renderBlock (processor).isEmpty());
        file.deleteFile();
    }

    // Momentary capture loss releases through the editor wiring.
    {
        ChordTextAudioProcessor processor;
        configureRootPosition (processor, "C");
        processor.setPlayHead (nullptr);
        processor.prepareToPlay (48000.0, 512);

        ChordTextAudioProcessorEditor editor (processor, UiSettings { juce::File() });
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        auto* sequence = dynamic_cast<ChordSequenceComponent*> (
            findEditorChild (editor, "chordSequence"));
        CHECK (sequence != nullptr);
        if (sequence == nullptr)
            return;
        // A deferred rebuild can finish before the editor's next timer tick.
        // Wait for the displayed revision, not an assumed 60 ms refresh time.
        for (int attempt = 0; attempt < 50
             && (sequence->getDisplayedRevision() == 0
                 || sequence->getDisplayedRevision() != processor.getPreviewCardRevision());
             ++attempt)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        CHECK (sequence->getDisplayedRevision() != 0
               && sequence->getDisplayedRevision() == processor.getPreviewCardRevision());
        auto* content = sequence->getViewedComponent();
        const auto centre = sequence->cardBounds (0).getCentre();

        content->mouseDown (makeMouseEvent (
            *content, centre.toFloat(), juce::ModifierKeys::leftButtonModifier));
        CHECK (! pitches (renderBlock (processor), true).empty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::momentary);

        content->mouseUp (makeMouseEvent (
            *content, juce::Point<float> (-50.0f, -50.0f),
            juce::ModifierKeys::leftButtonModifier));
        CHECK (pitches (renderBlock (processor), true).empty());
        CHECK (processor.getPreviewFeedback().mode == chordscript::PreviewMode::idle);
    }
}

} // namespace

void runChordPreviewChecks()
{
    checkPreviewExchange();
    checkPreviewExchangeStress();
    checkMomentaryAndHeldPreview();
    checkPreviewReplacementAndCoalescing();
    checkPreviewPlaybackPriority();
    checkPreviewBeforeRebuiltPlanDelivery();
    checkPreviewCancellation();
    checkPreviewTransportIndependence();
    checkPreviewStateAndFiles();
    checkPreviewCardGestures();
    checkPreviewScaledAndScrolledCards();
    checkPreviewBadgePadding();
    checkPreviewDisplayedRevision();
    checkPreviewEditorCleanup();
}
