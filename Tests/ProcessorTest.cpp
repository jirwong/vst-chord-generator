#include "Check.h"
#include "ChordPreviewChecks.h"
#include "Plugin/ClipDragComponent.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/UiSettings.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <thread>

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

static juce::AudioProcessorEditor* createTestEditor (ChordTextAudioProcessor& processor)
{
    return new ChordTextAudioProcessorEditor (processor, UiSettings {juce::File()});
}

static juce::Component* findEditorChild (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id)
        return &parent;
    for (auto* child : parent.getChildren())
        if (auto* match = findEditorChild (*child, id))
            return match;
    return nullptr;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    runChordPreviewChecks();

    // UI preferences are validated and shared through a local file, not project state.
    {
        const auto directory = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString());
        CHECK (directory.createDirectory().wasOk());
        const auto file = directory.getChildFile ("ui-settings.json");
        UiSettings settings (file);
        UiSettings otherProcess (file);
        CHECK (settings.loadUiScalePercent() == 100);
        CHECK (! file.exists());
        for (const auto percent : {75, 100, 125, 150})
        {
            CHECK (UiSettings::isSupportedScale (percent));
            CHECK (settings.saveUiScalePercent (percent).wasOk());
            CHECK (otherProcess.loadUiScalePercent() == percent);
            CHECK (juce::JSON::parse (file.loadFileAsString())["uiScalePercent"].isInt());
        }
        const auto previous = file.loadFileAsString();
        for (const auto percent : {0, 74, 80, 99, 151, 200})
        {
            CHECK (! UiSettings::isSupportedScale (percent));
            CHECK (settings.saveUiScalePercent (percent).failed());
            CHECK (file.loadFileAsString() == previous);
        }
        for (const auto* invalid : {"broken", "{}", "[]", "null", "{\"uiScalePercent\":\"125\"}",
                                   "{\"uiScalePercent\":125.5}", "{\"uiScalePercent\":100.0}",
                                   "{\"uiScalePercent\":90}", "{\"uiScalePercent\":null}"})
        {
            CHECK (file.replaceWithText (invalid));
            CHECK (settings.loadUiScalePercent() == 100);
            CHECK (file.loadFileAsString() == invalid);
        }
        const auto blockedParent = directory.getChildFile ("not-a-folder");
        CHECK (blockedParent.replaceWithText ("keep"));
        const UiSettings blocked (blockedParent.getChildFile ("ui-settings.json"));
        CHECK (blocked.loadUiScalePercent() == 100);
        CHECK (blocked.saveUiScalePercent (125).failed());
        CHECK (blockedParent.loadFileAsString() == "keep");
        const UiSettings disabled {juce::File()};
        CHECK (disabled.loadUiScalePercent() == 100);
        CHECK (disabled.saveUiScalePercent (125).failed());

        CHECK (settings.saveUiScalePercent (125).wasOk());
        const auto saved = file.loadFileAsString();
        CHECK (file.setReadOnly (true));
        CHECK (settings.saveUiScalePercent (150).failed());
        CHECK (file.loadFileAsString() == saved);
        CHECK (file.setReadOnly (false));

        // Independent writers start together against an absent preference file.
        for (int round = 0; round < 8; ++round)
        {
            const auto shared = directory.getChildFile ("concurrent-" + juce::String (round) + ".json");
            std::atomic<int> ready {0};
            std::atomic<bool> start {false};
            std::array<bool, 4> success {};
            std::array<std::thread, 4> writers;
            const std::array<int, 4> percentages {75, 100, 125, 150};
            for (size_t index = 0; index < writers.size(); ++index)
                writers[index] = std::thread ([&, index]
                {
                    ++ready;
                    while (! start.load())
                        std::this_thread::yield();
                    const auto result = UiSettings (shared).saveUiScalePercent (percentages[index]);
                    success[index] = result.wasOk();
                });
            while (ready.load() != 4)
                std::this_thread::yield();
            start = true;
            for (auto& writer : writers)
                writer.join();
            // Windows can reject overlapping replacements. A failed writer must
            // leave a complete value from one of the successful writers.
            CHECK (std::any_of (success.begin(), success.end(), [] (bool value) { return value; }));
            const auto value = juce::JSON::parse (shared.loadFileAsString())["uiScalePercent"];
            CHECK (value.isInt() && UiSettings::isSupportedScale (static_cast<int> (value)));
            bool savedBySuccessfulWriter = false;
            for (size_t index = 0; index < success.size(); ++index)
                savedBySuccessfulWriter = savedBySuccessfulWriter
                    || (success[index] && static_cast<int> (value) == percentages[index]);
            CHECK (savedBySuccessfulWriter);
        }
    }

    // User zoom scales logical content and composes with the host's display scale.
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString()).getChildFile ("scale.json");
        ChordTextAudioProcessor p;
        p.setProgressionText ("1(7), 6(7), 2(7), 5(7)");
        ChordTextAudioProcessorEditor editor (p, UiSettings (file));
        editor.setVisible (true); // JUCE hit testing excludes hidden components.
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
        auto* content = findEditorChild (editor, "editorContent");
        auto* label = dynamic_cast<juce::Label*> (findEditorChild (editor, "uiScaleLabel"));
        CHECK (selector != nullptr && content != nullptr);
        CHECK (label != nullptr && label->getText() == "UI SCALE");
        if (selector && content)
        {
            CHECK (selector->getNumItems() == 4);
            CHECK (selector->getText() == "100%");
            const int percentages[] {75, 100, 125, 150};
            const int widths[] {720, 960, 1200, 1440};
            const int heights[] {488, 650, 813, 975};
            for (int index = 0; index < 4; ++index)
            {
                selector->setSelectedItemIndex (index, juce::sendNotificationSync);
                const auto factor = static_cast<float> (percentages[index]) / 100.0f;
                CHECK (editor.getWidth() == widths[index]);
                CHECK (editor.getHeight() == heights[index]);
                CHECK (content->getWidth() == 960 && content->getHeight() == 650);
                CHECK_NEAR (content->getTransform().mat00, factor, 0.001f);
                CHECK (UiSettings (file).loadUiScalePercent() == percentages[index]);
                const auto selectorArea = editor.getLocalArea (selector, selector->getLocalBounds());
                CHECK (editor.getLocalBounds().contains (selectorArea));
                auto* footer = findEditorChild (editor, "footerStatus");
                CHECK (! selectorArea.intersects (editor.getLocalArea (footer, footer->getLocalBounds())));
                auto* loop = findEditorChild (editor, "loopButton");
                CHECK (editor.getLocalArea (loop, loop->getLocalBounds()).getRight() <= editor.getWidth() - 20);
                const auto point = selectorArea.getCentre();
                auto* hit = editor.getComponentAt (point);
                CHECK (hit == selector || selector->isParentOf (hit));
                for (const auto hostFactor : {1.25f, 1.5f})
                {
                    editor.setScaleFactor (hostFactor);
                    const auto hostTransform = editor.getTransform();
                    selector->setSelectedItemIndex ((index + 1) % 4, juce::sendNotificationSync);
                    selector->setSelectedItemIndex (index, juce::sendNotificationSync);
                    CHECK (editor.getTransform() == hostTransform);
                    const auto origin = content->localPointToGlobal (juce::Point<float> (0.0f, 0.0f));
                    const auto scaled = content->localPointToGlobal (juce::Point<float> (100.0f, 100.0f));
                    CHECK_NEAR (scaled.x - origin.x, 100.0f * factor * hostFactor, 0.01f);
                }
                editor.setScaleFactor (1.0f);
                const auto snapshotDir = juce::SystemStats::getEnvironmentVariable ("CHORDSCRIPT_UI_SNAPSHOT_DIR", {});
                if (snapshotDir.isNotEmpty())
                {
                    const auto capture = [&] (const char* state)
                    {
                        const juce::File directory (snapshotDir);
                        CHECK (directory.createDirectory().wasOk());
                        const auto name = "ui-scale-" + juce::String (percentages[index]) + "-" + state + ".png";
                        juce::FileOutputStream output (directory.getChildFile (name));
                        CHECK (output.openedOk());
                        output.setPosition (0);
                        output.truncate();
                        CHECK (juce::PNGImageFormat().writeImageToStream (
                            editor.createComponentSnapshot (editor.getLocalBounds()), output));
                    };
                    capture ("valid");
                    auto* text = dynamic_cast<juce::TextEditor*> (findEditorChild (editor, "progressionEditor"));
                    auto* regenerate = dynamic_cast<juce::TextButton*> (findEditorChild (editor, "regenerateButton"));
                    text->setText ("1645", juce::dontSendNotification);
                    regenerate->triggerClick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
                    capture ("invalid");
                    text->setText ("1(7), 6(7), 2(7), 5(7)", juce::dontSendNotification);
                    regenerate->triggerClick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
                }
            }
        }
    }

    // Viewport scrolling uses logical coordinates and survives a scale change.
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString()).getChildFile ("scroll.json");
        ChordTextAudioProcessor p;
        juce::String progression;
        for (int i = 0; i < 32; ++i) progression += "1, ";
        p.setProgressionText (progression);
        ChordTextAudioProcessorEditor editor (p, UiSettings (file));
        auto* viewport = dynamic_cast<juce::Viewport*> (findEditorChild (editor, "chordSequence"));
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
        CHECK (viewport->getViewedComponent()->getHeight() > viewport->getHeight());
        viewport->setViewPosition (0, 90);
        selector->setSelectedId (75, juce::sendNotificationSync);
        CHECK (viewport->getViewPositionY() == 90);
        selector->setSelectedId (150, juce::sendNotificationSync);
        CHECK (viewport->getViewPositionY() == 90);
        const auto visibleArea = editor.getLocalArea (viewport, viewport->getLocalBounds());
        CHECK (editor.getLocalBounds().contains (visibleArea));
    }

    // Preference changes resize one editor; another editor reads them only on open.
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString()).getChildFile ("shared.json");
        ChordTextAudioProcessor firstProcessor, secondProcessor;
        ChordTextAudioProcessorEditor first (firstProcessor, UiSettings (file));
        auto second = std::make_unique<ChordTextAudioProcessorEditor> (secondProcessor, UiSettings (file));
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (first, "uiScaleBox"));
        auto* otherSelector = dynamic_cast<juce::ComboBox*> (findEditorChild (*second, "uiScaleBox"));
        CHECK (selector != nullptr && otherSelector != nullptr);
        if (selector && otherSelector)
        {
            juce::MemoryBlock before, after;
            firstProcessor.getStateInformation (before);
            selector->setSelectedId (125, juce::sendNotificationSync);
            CHECK (first.getWidth() == 1200 && first.getHeight() == 813);
            CHECK (second->getWidth() == 960 && otherSelector->getSelectedId() == 100);
            firstProcessor.getStateInformation (after);
            CHECK (before == after);
            CHECK (! juce::AudioProcessor::getXmlFromBinary (after.getData(), static_cast<int> (after.getSize()))
                        ->toString().contains ("uiScalePercent"));
            CHECK (firstProcessor.getParameters().size() == 8);
            firstProcessor.setStateInformation (before.getData(), static_cast<int> (before.getSize()));
            CHECK (first.getWidth() == 1200 && selector->getSelectedId() == 125);
            second.reset();
            second = std::make_unique<ChordTextAudioProcessorEditor> (secondProcessor, UiSettings (file));
            CHECK (second->getWidth() == 1200);
            CHECK (UiSettings (file).saveUiScalePercent (75).wasOk());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            CHECK (first.getWidth() == 1200 && second->getWidth() == 1200);
            second.reset();
            second = std::make_unique<ChordTextAudioProcessorEditor> (secondProcessor, UiSettings (file));
            CHECK (second->getWidth() == 720 && second->getHeight() == 488);
        }
    }

    // A failed save leaves the selected zoom active and displays a local status error.
    {
        const auto directory = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString());
        CHECK (directory.createDirectory().wasOk());
        const auto blocked = directory.getChildFile ("not-a-directory");
        CHECK (blocked.replaceWithText ("keep"));
        const UiSettings settings (blocked.getChildFile ("settings.json"));
        ChordTextAudioProcessor p;
        ChordTextAudioProcessorEditor editor (p, settings);
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
        auto* clip = dynamic_cast<ClipDragComponent*> (findEditorChild (editor, "clipDrag"));
        clip->onStatus ("Export failed: " + juce::String::repeatedString ("long-folder-name/", 100));
        selector->setSelectedId (150, juce::sendNotificationSync);
        CHECK (editor.getWidth() == 1440 && editor.getHeight() == 975);
        auto* status = dynamic_cast<juce::Label*> (findEditorChild (editor, "footerStatus"));
        CHECK (status->getText().contains ("UI scale was not saved"));
        CHECK (status->getText().indexOf ("UI scale was not saved")
               < status->getText().indexOf ("Export failed:"));
        CHECK (p.getDiagnostics().empty());
        CHECK (blocked.loadFileAsString() == "keep");
        ChordTextAudioProcessorEditor reopened (p, settings);
        CHECK (reopened.getWidth() == 960);
    }

    // Zoom preserves pending text and caret selection, then the normal debounce applies it.
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString()).getChildFile ("editing.json");
        ChordTextAudioProcessor p;
        ChordTextAudioProcessorEditor editor (p, UiSettings (file));
        auto* text = dynamic_cast<juce::TextEditor*> (findEditorChild (editor, "progressionEditor"));
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
        text->setText ("Dm7", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        text->setHighlightedRegion ({1, 3});
        const auto caret = text->getCaretPosition();
        const auto selection = text->getHighlightedRegion();
        selector->setSelectedId (75, juce::sendNotificationSync);
        CHECK (text->getText() == "Dm7");
        CHECK (text->getCaretPosition() == caret && text->getHighlightedRegion() == selection);
        CHECK (p.getProgressionText() != "Dm7");
        auto* status = dynamic_cast<juce::Label*> (findEditorChild (editor, "sequenceStatus"));
        CHECK (status->getText() == "Updating preview");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        CHECK (p.getProgressionText() == "Dm7");
        CHECK (p.getLearningRows().size() == 1 && p.getLearningRows()[0].chord == "Dm7");
    }

    // Changing UI scale does not publish a musical plan or retrigger an active chord.
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings").getChildFile (juce::Uuid().toString()).getChildFile ("playback.json");
        ChordTextAudioProcessor p;
        p.setProgressionText ("C:8");
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);
        ChordTextAudioProcessorEditor editor (p, UiSettings (file));
        juce::AudioBuffer<float> audio (0, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK (noteOnPitches (midi).size() == 3);
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
        selector->setSelectedId (150, juce::sendNotificationSync);
        head.set (512.0 / 48000.0 * 2.0, true, 120.0);
        p.processBlock (audio, midi);
        CHECK (midi.isEmpty());
        head.set (1024.0 / 48000.0 * 2.0, false, 120.0);
        p.processBlock (audio, midi);
        CHECK (countNoteOffs (midi) == 3);
    }

    // The new selector preserves old normalization and restores old projects.
    {
        ChordTextAudioProcessor p;
        auto* legacy = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("mode"));
        auto* scale = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("scaleMode"));
        CHECK (legacy != nullptr && legacy->choices.size() == 2);
        CHECK (scale != nullptr && scale->choices.size() == 9);
        CHECK (legacy->convertFrom0to1 (1.0f) == 1.0f);
        CHECK (p.getParameters().getUnchecked (p.getParameters().size() - 2) == scale);
        if (scale != nullptr)
        {
            p.setProgressionText ("1, 2(7)");
            for (int index = 0; index < 9; ++index)
            {
                scale->setValueNotifyingHost (static_cast<float> (index) / 8.0f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                CHECK (choiceIndex (p, "scaleMode") == index);
                CHECK (p.getLearningRows().size() == 2);
                CHECK (p.getLearningRows()[1].numericDegree == 2);
            }
            legacy->setValueNotifyingHost (1.0f);
            scale->setValueNotifyingHost (2.0f / 8.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (choiceIndex (p, "scaleMode") == 2);
            CHECK (p.getScaleSummary() == juce::String::fromUTF8 ("C Dorian \xc2\xb7 C D Eb F G A Bb"));

            legacy->setValueNotifyingHost (0.0f);
            legacy->setValueNotifyingHost (1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (choiceIndex (p, "scaleMode") == 1);

            std::thread automation ([&] { scale->setValueNotifyingHost (7.0f / 8.0f); });
            automation.join();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (choiceIndex (p, "scaleMode") == 7);
            CHECK (p.getLearningRows()[0].chord == "Cm");
            scale->setValueNotifyingHost (0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (choiceIndex (p, "mode") == 0);

            // Construct legacy state by removing the new parameter from a snapshot.
            auto oldState = p.apvts.copyState();
            oldState.removeChild (oldState.getChildWithProperty ("id", "scaleMode"), nullptr);
            oldState.getChildWithProperty ("id", "mode").setProperty ("value", 1.0f, nullptr);
            oldState.setProperty ("progressionText", "1", nullptr);
            juce::MemoryBlock bytes;
            juce::AudioProcessor::copyXmlToBinary (*oldState.createXml(), bytes);
            scale->setValueNotifyingHost (2.0f / 8.0f); // pending Dorian must not survive restore
            p.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (choiceIndex (p, "scaleMode") == 1);
            CHECK (p.getLearningRows()[0].chord == "Cm");

            // Saving before deferred synchronization must preserve the new selection.
            scale->setValueNotifyingHost (7.0f / 8.0f);
            p.getStateInformation (bytes);
            ChordTextAudioProcessor restored;
            restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            CHECK (choiceIndex (restored, "scaleMode") == 7);
            CHECK (restored.getScaleSummary().contains ("Harmonic Minor"));
        }
    }

    // Numeric cards and scale summary share the selected scale and retain stale analysis.
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("1(7), 6(7), 2(7), 5(7)");
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        auto* selector = dynamic_cast<juce::ComboBox*> (findEditorChild (*editor, "modeBox"));
        auto* summary = dynamic_cast<juce::Label*> (findEditorChild (*editor, "scaleSummary"));
        CHECK (selector != nullptr);
        CHECK (summary != nullptr);
        const auto snapshotDir = juce::SystemStats::getEnvironmentVariable ("CHORDSCRIPT_UI_SNAPSHOT_DIR", {});
        const auto snapshot = [&] (const char* name)
        {
            if (snapshotDir.isEmpty()) return;
            juce::File directory (snapshotDir);
            directory.createDirectory();
            juce::FileOutputStream stream (directory.getChildFile (name));
            if (stream.openedOk())
            {
                stream.setPosition (0);
                stream.truncate();
                juce::PNGImageFormat().writeImageToStream (
                    editor->createComponentSnapshot (editor->getLocalBounds()), stream);
            }
        };
        if (selector && summary)
        {
            CHECK (selector->getNumItems() == 9);
            CHECK (summary->getText() == p.getScaleSummary());
            CHECK (summary->getY() >= selector->getBottom());
            CHECK (summary->getRight() <= editor->getWidth() - 28);
            CHECK (p.getLearningRows()[2].numericDegree == 2);
            CHECK (p.getLearningRows()[2].degree == "ii7");
            auto* footer = dynamic_cast<juce::Label*> (findEditorChild (*editor, "footerStatus"));
            CHECK (footer != nullptr && footer->getText() == juce::String::fromUTF8 ("4 chords \xc2\xb7 MIDI ready"));
            snapshot ("numeric-major.png");
            selector->setSelectedItemIndex (2, juce::sendNotificationSync);
            for (int attempt = 0; attempt < 50
                 && ! summary->getText().contains ("C Dorian"); ++attempt)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            }
            CHECK (choiceIndex (p, "scaleMode") == 2);
            CHECK (p.getLearningRows()[0].chord == "Cm7");
            CHECK (summary->getText().contains ("C Dorian"));
            snapshot ("numeric-dorian.png");

            p.setProgressionText ("1645");
            selector->setSelectedItemIndex (7, juce::sendNotificationSync);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            CHECK (summary->getText().contains ("Harmonic Minor"));
            CHECK (p.getLearningRows()[0].chord == "Cm7");
            auto* status = dynamic_cast<juce::Label*> (findEditorChild (*editor, "sequenceStatus"));
            auto* errors = findEditorChild (*editor, "diagnosticsDisplay");
            CHECK (status != nullptr && status->getText() == "Last valid preview");
            CHECK (errors != nullptr && ! errors->getBounds().intersects (summary->getBounds()));
            auto* staleSequence = findEditorChild (*editor, "chordSequence");
            const auto staleImage = staleSequence->createComponentSnapshot (staleSequence->getLocalBounds());
            CHECK (staleImage.getPixelAt (10, 80).getRed() < 0x1d);
            snapshot ("numeric-invalid.png");
            editor.reset (createTestEditor (p));
            summary = dynamic_cast<juce::Label*> (findEditorChild (*editor, "scaleSummary"));
            CHECK (summary != nullptr && summary->getText() == p.getScaleSummary());
        }
    }

    // Numeric and explicit chords must produce identical export and drag MIDI.
    {
        struct MidiExample { int mode; int tonic; const char* numeric; const char* explicitChords; };
        const MidiExample examples[] {
            {0, 0, "1(7):2, 6(7):1.5, 2(7):0.5, 5(7):4", "Cmaj7:2, Am7:1.5, Dm7:0.5, G7:4"},
            {7, 13, "1(7), 3(7), 5dom7", "AmMaj7, Cmaj7#5, E7"},
            {2, 0, "1(7), III, 5(7):8", "Cm7, Eb, Gm7:8"}
        };
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build/Tests/numeric-output");
        CHECK (directory.createDirectory().wasOk());
        for (const auto& example : examples)
            for (const auto rootPosition : {false, true})
            {
                ChordTextAudioProcessor numeric, explicitInput;
                for (auto* processor : {&numeric, &explicitInput})
                {
                    processor->apvts.getParameter ("scaleMode")->setValueNotifyingHost (static_cast<float> (example.mode) / 8.0f);
                    processor->apvts.getParameter ("key")->setValueNotifyingHost (static_cast<float> (example.tonic) / 16.0f);
                    processor->apvts.getParameter ("voicing")->setValueNotifyingHost (rootPosition ? 1.0f : 0.0f);
                }
                numeric.setProgressionText (example.numeric);
                explicitInput.setProgressionText (example.explicitChords);
                CHECK (numeric.getDiagnostics().empty());
                CHECK (explicitInput.getDiagnostics().empty());
                juce::String error;
                const auto numericFile = directory.getNonexistentChildFile ("numeric", ".mid", false);
                CHECK (numeric.exportMidiFile (numericFile, error));
                const auto explicitFile = directory.getNonexistentChildFile ("explicit", ".mid", false);
                CHECK (explicitInput.exportMidiFile (explicitFile, error));
                juce::MemoryBlock numericBytes, explicitBytes;
                CHECK (numericFile.loadFileAsData (numericBytes));
                CHECK (explicitFile.loadFileAsData (explicitBytes));
                CHECK (numericBytes == explicitBytes);
                const auto numericDrag = numeric.createDragClipFile (error);
                const auto explicitDrag = explicitInput.createDragClipFile (error);
                numericBytes.reset();
                explicitBytes.reset();
                CHECK (numericDrag.loadFileAsData (numericBytes));
                CHECK (explicitDrag.loadFileAsData (explicitBytes));
                CHECK (numericBytes == explicitBytes);
                juce::MemoryInputStream stream (numericBytes, false);
                juce::MidiFile readBack;
                CHECK (readBack.readFrom (stream));
                CHECK (readBack.getNumTracks() == 1);
                CHECK (readBack.getTrack (0)->getNumEvents() > 0);
            }
    }

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

    // Bass octave choices remain per-instance state and follow the stable parameters.
    {
        ChordTextAudioProcessor p;
        auto* bass = dynamic_cast<juce::AudioParameterChoice*> (
            p.apvts.getParameter ("bassOctaves"));
        CHECK (bass != nullptr);
        CHECK (bass != nullptr && bass->choices.size() == 4);
        CHECK (bass != nullptr && bass->choices[0] == "Off");
        CHECK (bass != nullptr && bass->choices[1] == "1 octave down");
        CHECK (bass != nullptr && bass->choices[2] == "2 octaves down");
        CHECK (bass != nullptr && bass->choices[3] == "3 octaves down");
        CHECK (bass != nullptr && bass->getIndex() == 0 && bass->isAutomatable());
        CHECK (p.getParameters().getLast() == bass);
        CHECK (p.getParameters().getUnchecked (p.getParameters().size() - 2)
               == p.apvts.getParameter ("scaleMode"));
        if (bass != nullptr)
        {
            for (int offset = 0; offset <= 3; ++offset)
            {
                bass->setValueNotifyingHost (static_cast<float> (offset) / 3.0f);
                juce::MemoryBlock state;
                p.getStateInformation (state);
                ChordTextAudioProcessor restored;
                restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                CHECK (choiceIndex (restored, "bassOctaves") == offset);
            }

            auto oldState = p.apvts.copyState();
            oldState.removeChild (oldState.getChildWithProperty ("id", "bassOctaves"), nullptr);
            juce::MemoryBlock oldBytes;
            juce::AudioProcessor::copyXmlToBinary (*oldState.createXml(), oldBytes);
            bass->setValueNotifyingHost (1.0f);
            p.setStateInformation (oldBytes.getData(), static_cast<int> (oldBytes.getSize()));
            CHECK (choiceIndex (p, "bassOctaves") == 0);
        }
    }

    // Plugin rows and live generation share the final custom chord and bass pitches.
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.apvts.getParameter ("voicing")->setValueNotifyingHost (1.0f);
        p.setProgressionText ("[C5,E4,G4]:2");
        if (auto* bass = p.apvts.getParameter ("bassOctaves"))
            bass->setValueNotifyingHost (1.0f / 3.0f);
        p.rebuildPlan();

        const auto rows = p.getLearningRows();
        CHECK (p.getDiagnostics().empty());
        CHECK (rows.size() == 1);
        if (! rows.empty())
        {
            CHECK (rows[0].chord == "C custom");
            CHECK (rows[0].degree == "custom");
            CHECK (rows[0].formula == "[C5,E4,G4]");
            CHECK (rows[0].notes == "E4 G4 C5");
            CHECK (rows[0].bass == "C4");
            CHECK (rows[0].durationBeats == 2.0);
        }

        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK ((sortedUnique (noteOnPitches (midi)) == std::vector<int> {60, 64, 67, 72}));
    }

    // Exact custom-note spellings stay attached to their pitches across octaves.
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("[C#4,Db5]");
        p.rebuildPlan();

        const auto rows = p.getLearningRows();
        CHECK (rows.size() == 1);
        if (! rows.empty())
            CHECK (rows[0].notes == "C#4 Db5");
    }

    // Pitch errors keep the prior live plan and rows, but block stale output paths.
    {
        FakePlayHead head;
        head.set (0.0, true, 120.0);
        ChordTextAudioProcessor p;
        p.apvts.getParameter ("voicing")->setValueNotifyingHost (1.0f);
        p.setProgressionText ("C");
        if (auto* bass = p.apvts.getParameter ("bassOctaves"))
            bass->setValueNotifyingHost (1.0f / 3.0f);
        p.rebuildPlan();
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (audio, midi);
        CHECK ((sortedUnique (noteOnPitches (midi)) == std::vector<int> {36, 48, 52, 55}));
        const auto oldRevision = p.getPreviewCardRevision();

        p.setProgressionText ("[C-1]");
        const auto rows = p.getLearningRows();
        CHECK (! p.getDiagnostics().empty());
        CHECK (rows.size() == 1 && rows[0].chord == "C");
        CHECK (rows.size() == 1 && rows[0].bass == "C2");
        CHECK (p.getPreviewCardRevision() == 0);
        CHECK (p.beginMomentaryPreview (0, oldRevision) == 0);

        head.set (4.0, true, 120.0);
        p.processBlock (audio, midi);
        CHECK ((sortedUnique (noteOnPitches (midi)) == std::vector<int> {36, 48, 52, 55}));

        const auto outputDirectory = juce::File::getCurrentWorkingDirectory()
                                         .getChildFile ("build/Tests");
        const auto exportFile = outputDirectory.getNonexistentChildFile (
            "invalid-bass", ".mid", false);
        juce::String error;
        CHECK (! p.exportMidiFile (exportFile, error));
        CHECK (! exportFile.existsAsFile());
        const auto drag = p.createDragClipFile (error, outputDirectory);
        CHECK (! drag.existsAsFile());

        p.setProgressionText ("");
        CHECK (p.getDiagnostics().empty());
        CHECK (p.getLearningRows().empty());
        p.setProgressionText ("Dm");
        CHECK (p.getDiagnostics().empty());
        CHECK (p.getLearningRows().size() == 1);
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
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        if (editor != nullptr)
        {
            CHECK (editor->getWidth() == 960);
            CHECK (editor->getHeight() == 650);
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
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);
        CHECK (editor->getWidth() == 960);
        CHECK (editor->getHeight() == 650);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "regenerateButton"));
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

    // the sequence is exposed as a chord-card view
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        auto* sequence = findEditorChild (*editor, "chordSequence");
        CHECK (sequence != nullptr);

        if (sequence != nullptr)
        {
            CHECK (sequence->isVisible());
            CHECK (sequence->getComponentID() == "chordSequence");
        }
    }

    // compact editor states keep diagnostics beside the editor and actions in sync
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "regenerateButton"));
        auto* diagnostics = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "diagnosticsDisplay"));
        auto* sequenceStatus = dynamic_cast<juce::Label*> (
            findEditorChild (*editor, "sequenceStatus"));
        auto* sequence = findEditorChild (*editor, "chordSequence");
        auto* exportButton = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "exportButton"));
        auto* revealButton = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "revealFile"));
        auto* clip = dynamic_cast<ClipDragComponent*> (
            findEditorChild (*editor, "clipDrag"));
        auto* loop = findEditorChild (*editor, "loopButton");

        CHECK (progression != nullptr);
        CHECK (regenerate != nullptr);
        CHECK (diagnostics != nullptr);
        CHECK (sequenceStatus != nullptr);
        CHECK (sequence != nullptr);
        CHECK (exportButton != nullptr);
        CHECK (revealButton != nullptr);
        CHECK (clip != nullptr);
        CHECK (loop != nullptr);

        if (progression != nullptr && regenerate != nullptr && diagnostics != nullptr
            && sequenceStatus != nullptr && sequence != nullptr && exportButton != nullptr
            && revealButton != nullptr && clip != nullptr && loop != nullptr)
        {
            CHECK (! diagnostics->isVisible());
            CHECK (sequenceStatus->getText() == juce::String::fromUTF8 ("\xe2\x9c\x93  Valid progression"));
            CHECK (exportButton->isEnabled());
            CHECK (clip->isEnabled());
            CHECK (! revealButton->isEnabled());
            CHECK (clip->getHeight() == exportButton->getHeight());
            CHECK (clip->getHeight() == revealButton->getHeight());
            CHECK (loop->getRight() <= editor->getWidth() - 28);

            progression->setText ("H", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (diagnostics->isVisible());
            CHECK (diagnostics->getY() > progression->getBottom());
            CHECK (diagnostics->getBottom() <= sequenceStatus->getY());
            CHECK (sequenceStatus->getText() == "Last valid preview");
            CHECK (! exportButton->isEnabled());
            CHECK (! clip->isEnabled());

            progression->setText ("", juce::dontSendNotification);
            regenerate->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            CHECK (! diagnostics->isVisible());
            CHECK (sequenceStatus->getText() == "Enter a progression");
            CHECK (! exportButton->isEnabled());
            CHECK (! clip->isEnabled());

            progression->setText ("G7", true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            CHECK (sequenceStatus->getText() == "Updating preview");
            CHECK (! exportButton->isEnabled());
            CHECK (! clip->isEnabled());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            CHECK (sequenceStatus->getText() == juce::String::fromUTF8 ("\xe2\x9c\x93  Valid progression"));
            CHECK (exportButton->isEnabled());
            CHECK (clip->isEnabled());
        }
    }

    // Bass control stays beside the scale summary at every UI scale.
    {
        ChordTextAudioProcessor p;
        p.setProgressionText ("[C5,E4,G4]");
        const auto settingsDirectory = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("build/Tests/ui-settings")
            .getChildFile (juce::Uuid().toString());
        CHECK (settingsDirectory.createDirectory().wasOk());
        ChordTextAudioProcessorEditor editor (
            p, UiSettings (settingsDirectory.getChildFile ("bass-ui.json")));
        editor.setVisible (true);

        auto* scale = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "uiScaleBox"));
        auto* summary = dynamic_cast<juce::Label*> (findEditorChild (editor, "scaleSummary"));
        auto* bassLabel = dynamic_cast<juce::Label*> (findEditorChild (editor, "bassLabel"));
        auto* bass = dynamic_cast<juce::ComboBox*> (findEditorChild (editor, "bassBox"));
        auto* loop = findEditorChild (editor, "loopButton");
        const auto snapshotDirectory = juce::SystemStats::getEnvironmentVariable (
            "CHORDSCRIPT_UI_SNAPSHOT_DIR", {});
        const auto capture = [&] (const juce::String& name)
        {
            if (snapshotDirectory.isEmpty())
                return;
            juce::File directory (snapshotDirectory);
            CHECK (directory.createDirectory().wasOk());
            juce::FileOutputStream output (directory.getChildFile (name + ".png"));
            CHECK (output.openedOk());
            output.setPosition (0);
            output.truncate();
            CHECK (juce::PNGImageFormat().writeImageToStream (
                editor.createComponentSnapshot (editor.getLocalBounds()), output));
        };

        CHECK (scale != nullptr && summary != nullptr && bassLabel != nullptr
               && bass != nullptr && loop != nullptr);
        if (scale != nullptr && summary != nullptr && bassLabel != nullptr
            && bass != nullptr && loop != nullptr)
        {
            CHECK (bassLabel->getText() == "BASS");
            CHECK (bass->getNumItems() == 4);
            CHECK (bass->getSelectedId() == 1);
            CHECK (choiceIndex (p, "bassOctaves") == 0);

            bass->setSelectedId (2, juce::sendNotificationSync);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            for (const auto percent : {75, 100, 125, 150})
            {
                scale->setSelectedId (percent, juce::sendNotificationSync);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                CHECK (summary->getRight() + 12 <= bassLabel->getX());
                CHECK (bassLabel->getY() >= bass->getY());
                CHECK (bassLabel->getBottom() <= bass->getBottom());
                CHECK (bass->getRight() == 932);
                CHECK (loop->getRight() <= 932);
                CHECK (choiceIndex (p, "bassOctaves") == 1);
                CHECK (p.getLearningRows().size() == 1);
                CHECK (p.getLearningRows()[0].chord == "C custom");
                CHECK (p.getLearningRows()[0].formula == "[C5,E4,G4]");
                CHECK (p.getLearningRows()[0].bass == "C4");
                CHECK (p.getLearningRows()[0].notes.contains ("C5"));
                capture ("bass-custom-" + juce::String (percent));

                p.setProgressionText ("[C-1]");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                CHECK (! p.getDiagnostics().empty());
                capture ("bass-invalid-" + juce::String (percent));
                p.setProgressionText ("[C5,E4,G4]");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            }

            scale->setSelectedId (100, juce::sendNotificationSync);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            juce::String longFormula = "[";
            for (int index = 0; index < 128; ++index)
            {
                if (index != 0)
                    longFormula += ",";
                longFormula += "1";
            }
            longFormula += "]";
            p.setProgressionText (longFormula);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (p.getLearningRows().size() == 1);
            CHECK (p.getLearningRows()[0].formula == longFormula);
            capture ("bass-long-formula-100");

            p.setProgressionText ("[C4,C3]");
            CHECK (p.getLearningRows().size() == 1);
            CHECK (p.getLearningRows()[0].bass == "C3");
            CHECK (p.getLearningRows()[0].notes == "C3 C4");
        }
    }

    // the editor distinguishes unavailable, stopped, and active transport states
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        auto* transport = dynamic_cast<juce::Label*> (
            findEditorChild (*editor, "transportStatus"));
        CHECK (transport != nullptr);

        if (transport != nullptr)
        {
            CHECK (transport->getText().contains ("No transport available"));

            FakePlayHead head;
            p.setPlayHead (&head);
            p.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buffer (0, 512);
            juce::MidiBuffer midi;

            head.set (0.0, false, 120.0);
            p.processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            CHECK (transport->getText().contains ("Transport stopped"));

            head.set (0.0, true, 120.0);
            p.processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            CHECK (transport->getText().contains ("Transport playing"));
        }
    }

    // auto-parse applies an editor edit after the debounce
    {
        ChordTextAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
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
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        auto* progression = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "regenerateButton"));
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
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        auto* clip = dynamic_cast<ClipDragComponent*> (
            findEditorChild (*editor, "clipDrag"));
        auto* progression = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
        auto* regenerate = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "regenerateButton"));
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
        std::unique_ptr<juce::AudioProcessorEditor> editor (createTestEditor (p));
        CHECK (editor != nullptr);

        auto* clip = dynamic_cast<ClipDragComponent*> (
            findEditorChild (*editor, "clipDrag"));
        auto* reveal = dynamic_cast<juce::TextButton*> (
            findEditorChild (*editor, "revealFile"));
        auto* progression = dynamic_cast<juce::TextEditor*> (
            findEditorChild (*editor, "progressionEditor"));
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
