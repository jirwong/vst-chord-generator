#include "Check.h"
#include "Plugin/ClipDragComponent.h"
#include "Plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <memory>
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
        ++positionCalls;

        PositionInfo info;
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        return info;
    }

    int getPositionCalls() const { return positionCalls; }

private:
    double ppq = 0.0;
    bool playing = true;
    double bpm = 120.0;
    mutable int positionCalls = 0;
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

static std::vector<int> noteOffPitches (const juce::MidiBuffer& midi)
{
    std::vector<int> result;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOff())
            result.push_back (metadata.getMessage().getNoteNumber());
    return result;
}

static std::vector<int> sortedUnique (std::vector<int> values)
{
    std::sort (values.begin(), values.end());
    values.erase (std::unique (values.begin(), values.end()), values.end());
    return values;
}

static bool noteOffsPrecedeNoteOns (const juce::MidiBuffer& midi)
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

static bool allEventsAtSample (const juce::MidiBuffer& midi, int sample)
{
    for (const auto metadata : midi)
        if (metadata.samplePosition != sample)
            return false;

    return true;
}

static int choiceIndex (ChordTextAudioProcessor& p, const char* id)
{
    auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id));
    return parameter != nullptr ? parameter->getIndex() : -1;
}

static bool readFirstTempo (const juce::File& file, double& secondsPerQuarter)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return false;

    juce::MidiFile midiFile;

    if (! midiFile.readFrom (in))
        return false;

    for (int track = 0; track < midiFile.getNumTracks(); ++track)
    {
        const auto* sequence = midiFile.getTrack (track);

        for (int index = 0; index < sequence->getNumEvents(); ++index)
        {
            const auto message = sequence->getEventPointer (index)->message;

            if (message.isTempoMetaEvent())
            {
                secondsPerQuarter = message.getTempoSecondsPerQuarterNote();
                return true;
            }
        }
    }

    return false;
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
        CHECK (p.isPlayheadAvailable());

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
            {
                CHECK (message.getChannel() == 1);
                CHECK (message.getVelocity() == 100);
            }
        }
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
        CHECK ((sortedUnique (noteOffPitches (stopped)) == std::vector<int> {48, 52, 55, 59}));
        CHECK (! p.isTransportAvailable());
        CHECK (p.isPlayheadAvailable());
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
        CHECK (noteOnPitches (bypassed).empty());
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

    // parameter layout defaults and default learning rows
    {
        ChordTextAudioProcessor p;

        CHECK (p.apvts.getParameter ("key") != nullptr);
        CHECK (p.apvts.getParameter ("mode") != nullptr);
        CHECK (p.apvts.getParameter ("voicing") != nullptr);
        CHECK (p.apvts.getParameter ("range") != nullptr);
        CHECK (p.apvts.getParameter ("velocity") != nullptr);
        CHECK (p.apvts.getParameter ("loop") != nullptr);

        CHECK (choiceIndex (p, "key") == 0);
        CHECK (choiceIndex (p, "mode") == 0);
        CHECK (choiceIndex (p, "voicing") == 0);
        CHECK (choiceIndex (p, "range") == 1);

        auto* key = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("key"));
        auto* mode = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("mode"));
        auto* voicing = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("voicing"));
        auto* range = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("range"));
        auto* velocity = dynamic_cast<juce::AudioParameterInt*> (p.apvts.getParameter ("velocity"));
        auto* loop = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter ("loop"));

        CHECK (key != nullptr && key->getCurrentChoiceName() == "C");
        CHECK (mode != nullptr && mode->getCurrentChoiceName() == "Major");
        CHECK (voicing != nullptr && voicing->getCurrentChoiceName() == "Voice-led");
        CHECK (range != nullptr && range->getCurrentChoiceName() == "C3-C6");
        CHECK (velocity != nullptr && velocity->get() == 100);
        CHECK (loop != nullptr && loop->get());

        const auto rows = p.getLearningRows();
        CHECK (rows.size() == 4);
        CHECK (rows.size() == 4 && rows[0].chord == "Cmaj7");
        CHECK (rows.size() == 4 && rows[0].degree == "Imaj7");
        CHECK (rows.size() == 4 && rows[0].notes == "C E G B");
        CHECK (p.getProgressionText() == "Cmaj7, Am7, Dm7, G7");
        CHECK (p.getProgramName (0) == "Default");
    }

    // key change updates the parsed rows and generated pitches
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.setProgressionText ("i, V");
        p.apvts.getParameter ("key")->setValueNotifyingHost (13.0f / 16.0f); // A
        p.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);        // Minor
        p.rebuildPlan();
        p.prepareToPlay (48000.0, 512);

        const auto rows = p.getLearningRows();
        CHECK (rows.size() == 2);

        if (rows.size() >= 2)
        {
            CHECK (rows[0].chord == "Am");
            CHECK (rows[1].chord == "E");
        }

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK ((noteOnPitches (midi) == std::vector<int> {57, 60, 64}));
    }

    // invalid text keeps the last valid plan and reports diagnostics
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("C");
        CHECK (p.getLearningRows().size() == 1);
        p.setProgressionText ("H");
        CHECK (p.getLearningRows().size() == 1);
        CHECK (! p.getDiagnostics().empty());
        CHECK (p.getDiagnostics().front().token == "H");

        FakePlayHead head;
        head.set (0.0, true, 120.0);
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK ((noteOnPitches (midi) == std::vector<int> {48, 52, 55}));
    }

    // state round-trip restores text, key, and the rebuilt rows
    {
        ChordTextAudioProcessor a;
        a.setProgressionText ("i, V");
        a.apvts.getParameter ("key")->setValueNotifyingHost (13.0f / 16.0f); // A
        a.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);        // Minor
        a.rebuildPlan();

        juce::MemoryBlock state;
        a.getStateInformation (state);

        ChordTextAudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (b.getProgressionText() == "i, V");
        const auto restoredRows = b.getLearningRows();
        CHECK (restoredRows.size() == 2);

        if (restoredRows.size() >= 2)
            CHECK (restoredRows[0].chord == "Am");

        CHECK (choiceIndex (b, "key") == 13);
        CHECK (choiceIndex (b, "mode") == 1);
    }

    // state round-trip restores voicing, range, velocity, and loop
    {
        ChordTextAudioProcessor a;
        a.setProgressionText ("Cmaj7, Dm7");
        a.apvts.getParameter ("voicing")->setValueNotifyingHost (1.0f); // Root position
        a.apvts.getParameter ("range")->setValueNotifyingHost (0.0f);   // C2-C5
        a.apvts.getParameter ("velocity")->setValueNotifyingHost (0.5f); // 64
        a.apvts.getParameter ("loop")->setValueNotifyingHost (0.0f);    // Off

        juce::MemoryBlock state;
        a.getStateInformation (state);

        ChordTextAudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());

        CHECK (choiceIndex (b, "voicing") == 1);
        CHECK (choiceIndex (b, "range") == 0);
        auto* velocity = dynamic_cast<juce::AudioParameterInt*> (b.apvts.getParameter ("velocity"));
        auto* loop = dynamic_cast<juce::AudioParameterBool*> (b.apvts.getParameter ("loop"));
        CHECK (velocity != nullptr && velocity->get() == 64);
        CHECK (loop != nullptr && ! loop->get());
    }

    // voicing mode changes the generated pitch set
    {
        FakePlayHead head;
        head.set (8.0, true, 120.0);

        ChordTextAudioProcessor voiceLed;
        voiceLed.setPlayHead (&head);
        voiceLed.setProgressionText ("Cmaj7, Am7, Dm7");
        voiceLed.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer voiceLedMidi;
        voiceLed.processBlock (audio, voiceLedMidi);
        CHECK ((noteOnPitches (voiceLedMidi) == std::vector<int> {57, 60, 62, 65}));

        ChordTextAudioProcessor rootPosition;
        rootPosition.setPlayHead (&head);
        rootPosition.setProgressionText ("Cmaj7, Am7, Dm7");
        rootPosition.apvts.getParameter ("voicing")->setValueNotifyingHost (1.0f);
        rootPosition.rebuildPlan();
        rootPosition.prepareToPlay (48000.0, 512);
        juce::MidiBuffer rootMidi;
        rootPosition.processBlock (audio, rootMidi);
        CHECK ((noteOnPitches (rootMidi) == std::vector<int> {50, 53, 57, 60}));
    }

    // range changes the generated pitch set
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        juce::AudioBuffer<float> audio (2, 512);

        ChordTextAudioProcessor low;
        low.setPlayHead (&head);
        low.setProgressionText ("Cmaj7");
        low.apvts.getParameter ("range")->setValueNotifyingHost (0.0f); // C2-C5
        low.rebuildPlan();
        low.prepareToPlay (48000.0, 512);
        juce::MidiBuffer lowMidi;
        low.processBlock (audio, lowMidi);
        CHECK ((noteOnPitches (lowMidi) == std::vector<int> {36, 40, 43, 47}));

        ChordTextAudioProcessor high;
        high.setPlayHead (&head);
        high.setProgressionText ("Cmaj7");
        high.apvts.getParameter ("range")->setValueNotifyingHost (1.0f); // C4-C7
        high.rebuildPlan();
        high.prepareToPlay (48000.0, 512);
        juce::MidiBuffer highMidi;
        high.processBlock (audio, highMidi);
        CHECK ((noteOnPitches (highMidi) == std::vector<int> {60, 64, 67, 71}));
    }

    // swapping the plan while playing releases old notes before the new chord
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.setProgressionText ("Cmaj7");
        p.prepareToPlay (48000.0, 512);

        const auto blockBeats = 512.0 * 120.0 / (60.0 * 48000.0);
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer first;
        p.processBlock (audio, first);
        CHECK ((noteOnPitches (first) == std::vector<int> {48, 52, 55, 59}));

        head.set (blockBeats, true, 120.0);
        p.setProgressionText ("Am7");
        p.setProgressionText ("Dm7");
        p.setProgressionText ("G7");
        juce::MidiBuffer swapped;
        p.processBlock (audio, swapped);

        CHECK ((sortedUnique (noteOffPitches (swapped)) == std::vector<int> {48, 52, 55, 59}));
        CHECK ((noteOnPitches (swapped) == std::vector<int> {55, 59, 62, 65}));
        CHECK (noteOffsPrecedeNoteOns (swapped));
    }

    // loop wrap releases the old chord before restarting the progression
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.setProgressionText ("C:0.5, G:0.5");
        p.prepareToPlay (48000.0, 512);

        const auto blockBeats = 512.0 * 120.0 / (60.0 * 48000.0);
        juce::AudioBuffer<float> audio (2, 512);

        // Advance without a seek until the block that crosses beat 1.0.
        for (int i = 0; i < 46; ++i)
        {
            head.set (i * blockBeats, true, 120.0);
            juce::MidiBuffer discarded;
            p.processBlock (audio, discarded);
        }

        head.set (46 * blockBeats, true, 120.0);
        juce::MidiBuffer wrapped;
        p.processBlock (audio, wrapped);

        CHECK ((sortedUnique (noteOffPitches (wrapped)) == std::vector<int> {55, 59, 62}));
        CHECK ((noteOnPitches (wrapped) == std::vector<int> {48, 52, 55}));
        CHECK (noteOffsPrecedeNoteOns (wrapped));

        head.set (47 * blockBeats, false, 120.0);
        juce::MidiBuffer stopped;
        p.processBlock (audio, stopped);
        CHECK (countNoteOffs (stopped) == 3);
        CHECK (noteOnPitches (stopped).empty());
        CHECK ((sortedUnique (noteOffPitches (stopped)) == std::vector<int> {48, 52, 55}));
    }

    // loop off stops generating after the progression end
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.apvts.getParameter ("loop")->setValueNotifyingHost (0.0f);
        p.prepareToPlay (48000.0, 512);

        const auto blockBeats = 512.0 * 120.0 / (60.0 * 48000.0);
        juce::AudioBuffer<float> audio (2, 512);
        auto lateNoteOns = false;
        auto endNoteOffs = 0;

        for (int i = 0; i <= 760; ++i)
        {
            head.set (i * blockBeats, true, 120.0);
            juce::MidiBuffer midi;
            p.processBlock (audio, midi);

            if (i >= 749 && i <= 751)
                endNoteOffs += countNoteOffs (midi);

            if (i >= 750 && ! noteOnPitches (midi).empty())
                lateNoteOns = true;
        }

        CHECK (! lateNoteOns);
        CHECK (endNoteOffs == 4);
        CHECK (p.getCurrentStep() == -1);
    }

    // releaseResources marks the transport unavailable
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK (p.isTransportAvailable());

        p.releaseResources();
        CHECK (! p.isTransportAvailable());
    }

    // re-preparing releases notes that were sounding at sample 0
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer first;
        p.processBlock (audio, first);
        CHECK ((noteOnPitches (first) == std::vector<int> {48, 52, 55, 59}));

        p.prepareToPlay (48000.0, 512);

        juce::MidiBuffer afterReprepare;
        p.processBlock (audio, afterReprepare);

        CHECK ((sortedUnique (noteOffPitches (afterReprepare))
                == std::vector<int> {48, 52, 55, 59}));
        CHECK (noteOffsPrecedeNoteOns (afterReprepare));

        for (const auto metadata : afterReprepare)
            if (metadata.getMessage().isNoteOff())
                CHECK (metadata.samplePosition == 0);
    }

    // starting mid-chord retriggers the active chord at sample 0
    {
        FakePlayHead head;
        head.set (9.5, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        CHECK ((noteOnPitches (midi) == std::vector<int> {57, 60, 62, 65}));
        CHECK (noteOffPitches (midi).empty());
        CHECK (allEventsAtSample (midi, 0));
        CHECK (p.getCurrentStep() == 2);
    }

    // missing playhead produces no notes and reports no transport
    {
        ChordTextAudioProcessor p;
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        CHECK (midi.isEmpty());
        CHECK (! p.isTransportAvailable());
        CHECK (! p.isPlayheadAvailable());
    }

    // export writes a readable file with tempo and paired notes
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.setProgressionText ("Cmaj7, G7");
        p.prepareToPlay (48000.0, 512);

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("chordtext-test", ".mid");
        const auto playHeadCallsBeforeExport = head.getPositionCalls();
        juce::String error;
        CHECK (p.exportMidiFile (file, error));
        CHECK (head.getPositionCalls() == playHeadCallsBeforeExport);
        CHECK (error.isEmpty());

        juce::FileInputStream in (file);
        CHECK (in.openedOk());
        juce::MidiFile midiFile;
        CHECK (midiFile.readFrom (in));
        CHECK (midiFile.getTimeFormat() == 960);
        CHECK (midiFile.getNumTracks() >= 1);

        int noteOns = 0, noteOffs = 0, tempoEvents = 0, timeSignatureEvents = 0;
        std::vector<double> noteOnTicks;
        for (int t = 0; t < midiFile.getNumTracks(); ++t)
        {
            const auto* track = midiFile.getTrack (t);
            for (int i = 0; i < track->getNumEvents(); ++i)
            {
                const auto* event = track->getEventPointer (i);
                const auto message = event->message;
                if (message.isNoteOn())
                {
                    ++noteOns;
                    noteOnTicks.push_back (message.getTimeStamp());
                }
                if (message.isNoteOff()) ++noteOffs;
                if (message.isTempoMetaEvent())
                {
                    ++tempoEvents;
                    CHECK_NEAR (message.getTempoSecondsPerQuarterNote(), 0.5, 0.001);
                }
                if (message.isTimeSignatureMetaEvent()) ++timeSignatureEvents;
            }
        }

        CHECK (noteOns == 8);
        CHECK (noteOffs == 8);
        CHECK (tempoEvents >= 1);
        CHECK (timeSignatureEvents >= 1);
        CHECK (noteOnTicks.size() == 8);

        if (noteOnTicks.size() >= 8)
        {
            for (int i = 0; i < 4; ++i)
                CHECK (noteOnTicks[(size_t) i] == 0.0);
            for (int i = 4; i < 8; ++i)
                CHECK (noteOnTicks[(size_t) i] == 4.0 * 960.0);
        }

        file.deleteFile();
    }

    // export uses the last tempo observed during processing when the playhead is gone
    {
        FakePlayHead head;
        head.set (0.0, true, 90.0);
        ChordTextAudioProcessor p;
        p.setPlayHead (&head);
        p.setProgressionText ("Cmaj7, G7");
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);

        // Export must use the cached tempo and must not query the playhead.
        p.setPlayHead (nullptr);

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("chordtext-90bpm", ".mid");
        juce::String error;
        CHECK (p.exportMidiFile (file, error));
        CHECK (error.isEmpty());

        double secondsPerQuarter = 0.0;
        CHECK (readFirstTempo (file, secondsPerQuarter));
        CHECK_NEAR (secondsPerQuarter, 60.0 / 90.0, 0.001);

        file.deleteFile();
    }

    // export falls back to 120 BPM when no host tempo has ever been observed
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("Cmaj7, G7");

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("chordtext-120bpm", ".mid");
        juce::String error;
        CHECK (p.exportMidiFile (file, error));
        CHECK (error.isEmpty());

        double secondsPerQuarter = 0.0;
        CHECK (readFirstTempo (file, secondsPerQuarter));
        CHECK_NEAR (secondsPerQuarter, 0.5, 0.001);

        file.deleteFile();
    }

    // export is rejected for invalid text and for an empty plan
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("chordtext-test", ".mid");

        ChordTextAudioProcessor invalid;
        invalid.setProgressionText ("H");
        juce::String invalidError;
        CHECK (! invalid.exportMidiFile (file, invalidError));
        CHECK (! invalidError.isEmpty());
        CHECK (! file.existsAsFile());

        ChordTextAudioProcessor empty;
        empty.setProgressionText ("");
        juce::String emptyError;
        CHECK (! empty.exportMidiFile (file, emptyError));
        CHECK (! emptyError.isEmpty());
        CHECK (! file.existsAsFile());
    }

    // editor smoke test: fixed size, created by the processor
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);

        if (editor != nullptr)
        {
            CHECK (editor->getWidth() == 760);
            CHECK (editor->getHeight() == 640);
        }
    }

    // learning rows carry the effective duration in beats
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("Cmaj7:2, G7");
        const auto rows = p.getLearningRows();
        CHECK (rows.size() == 2);

        if (rows.size() >= 2)
        {
            CHECK_NEAR (rows[0].durationBeats, 2.0, 1e-9);
            CHECK_NEAR (rows[1].durationBeats, 4.0, 1e-9);
        }
    }

    // editor exposes the new controls and can force a rebuild
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);
        CHECK (editor->getWidth() == 760);
        CHECK (editor->getHeight() == 640);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            editor->findChildWithID ("progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            editor->findChildWithID ("regenerateButton"));
        CHECK (progression != nullptr);
        CHECK (regenerate != nullptr);

        if (progression != nullptr && regenerate != nullptr)
        {
            progression->setText ("C", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            const auto rows = p.getLearningRows();
            CHECK (rows.size() == 1);

            if (rows.size() >= 1)
                CHECK (rows[0].chord == "C");

            progression->setText ("H", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (p.getLearningRows().size() == 1);
            CHECK (! p.getDiagnostics().empty());
        }
    }

    // the learning table exposes the Beats column
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);

        auto* table = dynamic_cast<juce::TableListBox*> (
            editor->findChildWithID ("learningTable"));
        CHECK (table != nullptr);

        if (table != nullptr)
        {
            CHECK (table->getHeader().getNumColumns (true) == 4);
            CHECK (table->getHeader().getColumnName (4) == "Beats");
        }
    }

    // auto-parse applies an editor edit after the debounce
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            editor->findChildWithID ("progressionEditor"));
        CHECK (progression != nullptr);

        if (progression != nullptr)
        {
            // sendNotification arms the editor's debounce; the timer must apply
            // the new text without a Regenerate click.
            progression->setText ("Am7, Dm7", true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

            const auto rows = p.getLearningRows();
            CHECK (rows.size() == 2);

            if (rows.size() >= 2)
            {
                CHECK (rows[0].chord == "Am7");
                CHECK (rows[1].chord == "Dm7");
            }
        }
    }

    // Regenerate with unchanged text responds to a parameter change
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            editor->findChildWithID ("progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            editor->findChildWithID ("regenerateButton"));
        CHECK (progression != nullptr);
        CHECK (regenerate != nullptr);

        if (progression != nullptr && regenerate != nullptr)
        {
            progression->setText ("I, V", true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

            auto rows = p.getLearningRows();
            CHECK (rows.size() == 2);

            if (rows.size() >= 2)
            {
                CHECK (rows[0].chord == "C");
                CHECK (rows[1].chord == "G");
            }

            // The key listener also posts an asynchronous rebuild, so this
            // verifies that Regenerate still runs cleanly and leaves rows that
            // reflect the new key; it does not isolate the rebuild to the click.
            p.apvts.getParameter ("key")->setValueNotifyingHost (3.0f / 16.0f); // D
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            rows = p.getLearningRows();
            CHECK (rows.size() == 2);

            if (rows.size() >= 2)
            {
                CHECK (rows[0].chord == "D");
                CHECK (rows[1].chord == "A");
            }

            CHECK (p.getDiagnostics().empty());
            CHECK (p.getProgressionText() == "I, V");
        }
    }

    // drag clip is one loop, with no tempo or time-signature metadata
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("Cmaj7:2, G7:2");

        juce::String error;
        const auto file = p.createDragClipFile (error);
        CHECK (error.isEmpty());
        CHECK (file.existsAsFile());
        CHECK (p.getLastDragClipFile() == file);
        CHECK (file.getParentDirectory().getFileName() == "Clips");
        CHECK (file.getParentDirectory().getParentDirectory().getFileName() == "ChordText");
        CHECK (file.getFileName().startsWith ("ChordText-"));

        juce::FileInputStream in (file);
        CHECK (in.openedOk());
        juce::MidiFile midiFile;
        CHECK (midiFile.readFrom (in));

        int noteOns = 0, noteOffs = 0, tempoEvents = 0, timeSignatureEvents = 0;
        std::vector<double> noteOnTicks;
        std::vector<double> noteOffTicks;

        for (int t = 0; t < midiFile.getNumTracks(); ++t)
        {
            const auto* track = midiFile.getTrack (t);
            for (int i = 0; i < track->getNumEvents(); ++i)
            {
                const auto* event = track->getEventPointer (i);
                const auto message = event->message;

                if (message.isNoteOn())
                {
                    ++noteOns;
                    noteOnTicks.push_back (event->message.getTimeStamp());
                    CHECK (message.getChannel() == 1);
                }
                if (message.isNoteOff())
                {
                    ++noteOffs;
                    noteOffTicks.push_back (event->message.getTimeStamp());
                }
                if (message.isTempoMetaEvent()) ++tempoEvents;
                if (message.isTimeSignatureMetaEvent()) ++timeSignatureEvents;
            }
        }

        CHECK (noteOns == 8);
        CHECK (noteOffs == 8);
        CHECK (tempoEvents == 0);
        CHECK (timeSignatureEvents == 0);
        CHECK (noteOnTicks.size() == 8);
        CHECK (noteOffTicks.size() == 8);

        if (noteOnTicks.size() >= 8 && noteOffTicks.size() >= 8)
        {
            for (int i = 0; i < 4; ++i)
                CHECK (noteOnTicks[(size_t) i] == 0.0);
            for (int i = 4; i < 8; ++i)
                CHECK (noteOnTicks[(size_t) i] == 2.0 * 960.0);
            for (int i = 0; i < 4; ++i)
                CHECK (noteOffTicks[(size_t) i] == 2.0 * 960.0);
            for (int i = 4; i < 8; ++i)
                CHECK (noteOffTicks[(size_t) i] == 4.0 * 960.0);
        }

        file.deleteFile();
    }

    // invalid text produces no drag file
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("H");
        juce::String error;
        const auto file = p.createDragClipFile (error);
        CHECK (! file.existsAsFile());
        CHECK (error.isNotEmpty());
    }

    // a failed drag attempt keeps the last successful clip
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("C");
        juce::String error;
        const auto file = p.createDragClipFile (error);
        CHECK (file.existsAsFile());
        CHECK (p.getLastDragClipFile() == file);

        p.setProgressionText ("H");
        const auto failed = p.createDragClipFile (error);
        CHECK (! failed.existsAsFile());
        CHECK (p.getLastDragClipFile() == file);

        file.deleteFile();
    }

    // clip preview follows the timer-driven refresh path
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);

        auto* clip = dynamic_cast<ClipDragComponent*> (
            editor->findChildWithID ("clipDrag"));
        auto* progression = dynamic_cast<juce::TextEditor*> (
            editor->findChildWithID ("progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            editor->findChildWithID ("regenerateButton"));
        CHECK (clip != nullptr);
        CHECK (progression != nullptr);
        CHECK (regenerate != nullptr);

        if (clip != nullptr && progression != nullptr && regenerate != nullptr)
        {
            // The default progression is valid, so the preview is enabled.
            CHECK (clip->isEnabled());

            progression->setText ("", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (! clip->isEnabled());

            progression->setText ("C", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (clip->isEnabled());

            progression->setText ("H", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (! clip->isEnabled());
            CHECK (! p.getDiagnostics().empty());
        }
    }

    // clip preview creates a file, enables Reveal, and rejects invalid text
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor != nullptr);

        auto* clip = dynamic_cast<ClipDragComponent*> (
            editor->findChildWithID ("clipDrag"));
        auto* reveal = dynamic_cast<juce::TextButton*> (
            editor->findChildWithID ("revealClip"));
        auto* progression = dynamic_cast<juce::TextEditor*> (
            editor->findChildWithID ("progressionEditor"));
        CHECK (clip != nullptr);
        CHECK (reveal != nullptr);
        CHECK (progression != nullptr);

        if (clip != nullptr && reveal != nullptr && progression != nullptr)
        {
            CHECK (! reveal->isEnabled());

            const auto file = clip->createClipFile();
            CHECK (file.existsAsFile());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (reveal->isEnabled());
            file.deleteFile();

            progression->setText ("H", juce::dontSendNotification);
            const auto invalidFile = clip->createClipFile();
            CHECK (invalidFile == juce::File());
            CHECK (! p.getDiagnostics().empty());
        }

        // Only the left mouse button may start a drag.
        if (clip != nullptr)
        {
            int createCalls = 0;
            clip->createClipFile = [&createCalls]
            {
                ++createCalls;
                return juce::File();
            };
            clip->setLoopInfo (16.0, true);

            const auto makeEvent = [clip] (juce::ModifierKeys modifiers,
                                            juce::Point<float> position)
            {
                return juce::MouseEvent (
                    juce::Desktop::getInstance().getMainMouseSource(),
                    position, modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                    clip, clip, juce::Time::getCurrentTime(),
                    juce::Point<float> (0.0f, 0.0f), juce::Time::getCurrentTime(),
                    1, true);
            };

            clip->mouseDown (makeEvent (juce::ModifierKeys::leftButtonModifier,
                                        juce::Point<float> (0.0f, 0.0f)));
            clip->mouseDrag (makeEvent (juce::ModifierKeys::rightButtonModifier,
                                        juce::Point<float> (20.0f, 20.0f)));
            CHECK (createCalls == 0);

            clip->mouseDrag (makeEvent (juce::ModifierKeys::leftButtonModifier,
                                        juce::Point<float> (20.0f, 20.0f)));
            CHECK (createCalls == 1);
        }
    }

    return reportAndExit();
}
