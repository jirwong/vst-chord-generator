#include "Check.h"
#include "Theory/Chord.h"
#include "Theory/ChordSymbolParser.h"
#include "Theory/ChordResolution.h"
#include "Theory/CustomChordParser.h"
#include "Theory/Key.h"
#include "Theory/NumericDegreeParser.h"
#include "Theory/MidiFileWriter.h"
#include "Theory/Note.h"
#include "Theory/Progression.h"
#include "Theory/RenderPlan.h"
#include "Theory/Voicer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace chordtext;

struct TestMidiEvent
{
    int tick = 0;
    bool noteOn = false;
    int pitch = 0;
    int velocity = 0;
    int channel = 1;
};

static int readU32 (const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (int (bytes[offset]) << 24) | (int (bytes[offset + 1]) << 16)
         | (int (bytes[offset + 2]) << 8) | int (bytes[offset + 3]);
}

static int readVlq (const std::vector<std::uint8_t>& bytes, std::size_t& offset)
{
    int value = 0;
    for (;;)
    {
        const auto byte = bytes[offset++];
        value = (value << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0)
            return value;
    }
}

static std::vector<TestMidiEvent> readNoteEvents (const std::vector<std::uint8_t>& bytes)
{
    std::vector<TestMidiEvent> result;
    auto offset = std::size_t (8 + readU32 (bytes, 4));
    const auto trackEnd = offset + 8 + readU32 (bytes, offset + 4);
    offset += 8;
    int tick = 0;
    int running = 0;

    while (offset < trackEnd)
    {
        tick += readVlq (bytes, offset);
        auto status = bytes[offset++];
        if (status < 0x80)
        {
            --offset;
            status = running;
        }
        else
        {
            running = status;
        }

        if (status == 0xFF)
        {
            const auto type = bytes[offset++];
            const auto length = readVlq (bytes, offset);
            offset += std::size_t (length);
            if (type == 0x2F)
                break;
            continue;
        }

        const auto type = status & 0xF0;
        const auto channel = (status & 0x0F) + 1;
        const auto data1 = int (bytes[offset++]);
        const auto data2 = (type == 0xC0 || type == 0xD0) ? 0 : int (bytes[offset++]);

        if (type == 0x90 && data2 > 0)
            result.push_back ({tick, true, data1, data2, channel});
        else if (type == 0x80 || (type == 0x90 && data2 == 0))
            result.push_back ({tick, false, data1, data2, channel});
    }

    return result;
}

static bool containsBytes (const std::vector<std::uint8_t>& bytes,
                           std::initializer_list<std::uint8_t> pattern)
{
    return std::search (bytes.begin(), bytes.end(), pattern.begin(), pattern.end())
        != bytes.end();
}

static void checkCustomChordParsing()
{
    const Key cMajor {{'C', 0}, Mode::major};
    const auto list = parseCustomChord ("[1,2,4,5]", cMajor, 0);
    CHECK (list.spec.has_value());
    if (list.spec && list.spec->customNotes)
    {
        CHECK (list.spec->root.name() == "C");
        CHECK (list.spec->customNotes->size() == 4);
        CHECK ((*list.spec->customNotes)[0].pitchClass.name() == "C");
        CHECK ((*list.spec->customNotes)[1].pitchClass.name() == "D");
        CHECK ((*list.spec->customNotes)[2].pitchClass.name() == "F");
        CHECK ((*list.spec->customNotes)[3].pitchClass.name() == "G");
        const Chord chord (*list.spec);
        CHECK (chord.symbol() == "C custom");
        CHECK (chord.tones().size() == 4);
        CHECK (chord.tones()[1].name() == "D");
        CHECK (chord.tones()[2].name() == "F");
        CHECK (chord.tones()[3].name() == "G");
    }
    else
    {
        CHECK (false);
    }

    const auto mixed = parseCustomChord ("[F3,1,3,5]", cMajor, 8);
    CHECK (mixed.spec.has_value());
    if (mixed.spec && mixed.spec->customNotes)
    {
        CHECK (mixed.spec->root.name() == "F");
        CHECK ((*mixed.spec->customNotes)[0].exactMidi == std::optional<int> {53});
    }
    else
    {
        CHECK (false);
    }

    const auto offsets = parseCustomChord ("[1@-1,1,8,14,F@+1]", cMajor, 0);
    CHECK (offsets.spec && offsets.spec->customNotes);
    if (offsets.spec && offsets.spec->customNotes)
    {
        const std::vector<int> expected {-1, 0, 1, 1, 1};
        CHECK (offsets.spec->customNotes->size() == expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index)
            CHECK ((*offsets.spec->customNotes)[index].octaveOffset == expected[index]);
    }

    const auto exactB = parseCustomChord ("[B3]", cMajor, 0);
    const auto flatThird = parseCustomChord ("[b3]", cMajor, 0);
    const auto enharmonics = parseCustomChord ("[B#3,Cb4]", cMajor, 0);
    CHECK (exactB.spec && exactB.spec->customNotes
           && (*exactB.spec->customNotes)[0].exactMidi == std::optional<int> {59});
    CHECK (flatThird.spec && flatThird.spec->customNotes
           && (*flatThird.spec->customNotes)[0].pitchClass.name() == "Eb");
    CHECK (enharmonics.spec && enharmonics.spec->customNotes
           && (*enharmonics.spec->customNotes)[0].exactMidi == std::optional<int> {60}
           && (*enharmonics.spec->customNotes)[1].exactMidi == std::optional<int> {59});
    CHECK (voicedNoteName ({'B', 1}, 60) == "B#3");
    CHECK (voicedNoteName ({'C', -1}, 59) == "Cb4");
    CHECK (voicedNoteName ({'C', 0}, 0) == "C-1");

    const auto cMajorMixed = parseCustomChord ("[1,3,5,F]", cMajor, 0);
    const auto dMajorMixed = parseCustomChord ("[1,3,5,F]", Key {{'D', 0}, Mode::major}, 0);
    CHECK (cMajorMixed.spec && cMajorMixed.spec->customNotes
           && dMajorMixed.spec && dMajorMixed.spec->customNotes
           && (*cMajorMixed.spec->customNotes)[3].pitchClass.name() == "F"
           && (*dMajorMixed.spec->customNotes)[3].pitchClass.name() == "F");

    for (const auto mode : allModes())
    {
        const Key key {{'D', 0}, mode};
        const auto scale = parseCustomChord ("[1,2,3,4,5,6,7]", key, 0);
        CHECK (scale.spec && scale.spec->customNotes);
        if (scale.spec && scale.spec->customNotes)
            for (int degree = 1; degree <= 7; ++degree)
                CHECK ((*scale.spec->customNotes)[static_cast<std::size_t> (degree - 1)]
                           .pitchClass.name() == key.degree (degree).name());

        const auto accidentals = parseCustomChord ("[b3,#4]", key, 0);
        CHECK (accidentals.spec && accidentals.spec->customNotes
               && (*accidentals.spec->customNotes)[0].pitchClass.semitone()
                      == key.degree (3).semitone() - 1
               && (*accidentals.spec->customNotes)[1].pitchClass.semitone()
                      == key.degree (4).semitone() + 1);
    }

    const Key cMinor {{'C', 0}, Mode::minor};
    const auto loweredMinorThird = parseCustomChord ("[b3]", cMinor, 0);
    CHECK (loweredMinorThird.spec && loweredMinorThird.spec->customNotes
           && (*loweredMinorThird.spec->customNotes)[0].pitchClass.semitone() == 2
           && (*loweredMinorThird.spec->customNotes)[0].pitchClass.name() == "Ebb");
}

static void checkCustomGrammarDiagnostics()
{
    const Key cMajor {{'C', 0}, Mode::major};
    const auto mixed = parseProgression ("C, [C-1, 1@-1, F#4]:2 | V", cMajor);
    CHECK (mixed.progression.has_value());
    if (mixed.progression)
    {
        CHECK (mixed.progression->steps.size() == 3);
        CHECK (mixed.progression->steps[1].durationBeats == 2.0);
        CHECK (mixed.progression->totalBeats() == 10.0);
    }

    for (const auto invalid : {"[]", "[1,]", "[1,,5]", "[1 3]", "[1,3", "[1,[3]]",
                               "[1@0]", "[8@-1]", "[F3@+1]", "[f#]", "[F##]",
                               "[C-1]junk", "[0]", "[15]"})
    {
        const auto result = parseProgression (invalid, cMajor);
        CHECK (! result.progression);
        CHECK (! result.diagnostics.empty());
    }

    std::string maximum = "[1";
    for (int entry = 1; entry < 128; ++entry)
        maximum += ",1";
    maximum += ']';
    CHECK (parseProgression (maximum, cMajor).progression.has_value());

    std::string tooMany = "[1";
    for (int entry = 1; entry < 129; ++entry)
        tooMany += ",1";
    tooMany += ']';
    CHECK (! parseProgression (tooMany, cMajor).progression);

    CHECK (! parseProgression ("1645", cMajor).progression);

    const auto emptyEntry = parseProgression ("C, [1,,5]", cMajor);
    CHECK (! emptyEntry.progression && ! emptyEntry.diagnostics.empty());
    if (! emptyEntry.diagnostics.empty())
        CHECK (emptyEntry.diagnostics.front().offset == 6);

    const auto exactLow = parseCustomChord ("[C-1,B-1]", cMajor, 0);
    const auto exactHigh = parseCustomChord ("[G9]", cMajor, 0);
    const auto belowMidi = parseCustomChord ("[Cb-1]", cMajor, 0);
    const auto aboveMidi = parseCustomChord ("[A9]", cMajor, 0);
    CHECK (exactLow.spec && exactLow.spec->customNotes
           && (*exactLow.spec->customNotes)[0].exactMidi == std::optional<int> {0}
           && (*exactLow.spec->customNotes)[1].exactMidi == std::optional<int> {11});
    CHECK (exactHigh.spec && exactHigh.spec->customNotes
           && (*exactHigh.spec->customNotes)[0].exactMidi == std::optional<int> {127});
    CHECK (! belowMidi.spec && ! belowMidi.diagnostics.empty());
    CHECK (! aboveMidi.spec && ! aboveMidi.diagnostics.empty());
}

static void checkCustomChordResolution()
{
    const Key cMajor {{'C', 0}, Mode::major};
    VoicingSettings rootPosition;
    rootPosition.mode = VoicingMode::rootPosition;

    const auto octaveStack = parseProgression ("[1@-1,1,8]", cMajor);
    const auto octaveResult = resolveProgression (*octaveStack.progression, rootPosition, 0);
    CHECK (octaveResult.chords && octaveResult.chords->size() == 1);
    if (octaveResult.chords)
    {
        CHECK (((*octaveResult.chords)[0].chordPitches == std::vector<int> {36, 48, 60}));
        CHECK ((*octaveResult.chords)[0].rootMidi == 36);
    }

    const auto enharmonicNotes = parseProgression ("[C#4,Db5]", cMajor);
    const auto enharmonicResult = resolveProgression (*enharmonicNotes.progression,
                                                       rootPosition, 0);
    CHECK (enharmonicResult.chords && enharmonicResult.chords->size() == 1);
    if (enharmonicResult.chords)
    {
        const auto& resolved = enharmonicResult.chords->front();
        CHECK ((resolved.chordPitches == std::vector<int> {61, 73}));
        CHECK (resolved.chordSpellings.size() == 2);
        if (resolved.chordSpellings.size() == 2)
        {
            CHECK (resolved.chordSpellings[0].name() == "C#");
            CHECK (resolved.chordSpellings[1].name() == "Db");
        }
    }

    const auto pinnedRoot = parseProgression ("[F3,1,3,5]", cMajor);
    const auto pinnedResult = resolveProgression (*pinnedRoot.progression, rootPosition, 1);
    CHECK (pinnedResult.chords && pinnedResult.chords->size() == 1);
    if (pinnedResult.chords)
    {
        CHECK ((*pinnedResult.chords)[0].rootMidi == 53);
        CHECK ((*pinnedResult.chords)[0].bassMidi == std::optional<int> {41});
        CHECK (((*pinnedResult.chords)[0].pitches == std::vector<int> {41, 53, 55, 60, 64}));
    }

    const auto rootIsRaisedEntry = parseProgression ("[8,1]", cMajor);
    const auto raisedResult = resolveProgression (*rootIsRaisedEntry.progression, rootPosition, 0);
    CHECK (raisedResult.chords && raisedResult.chords->size() == 1);
    if (raisedResult.chords)
    {
        CHECK (((*raisedResult.chords)[0].chordPitches == std::vector<int> {48, 60}));
        CHECK ((*raisedResult.chords)[0].rootMidi == 60);
    }

    const std::array<std::pair<int, int>, 3> ranges {{{36, 72}, {48, 84}, {60, 96}}};
    const auto progression = parseProgression ("[1,3,5], [2,4,6]", cMajor);
    for (const auto mode : {VoicingMode::rootPosition, VoicingMode::voiceLed})
        for (const auto range : ranges)
        {
            VoicingSettings settings {mode, range.first, range.second};
            const auto resolved = resolveProgression (*progression.progression, settings, 0);
            const auto baseline = voiceProgression (*progression.progression, settings);
            CHECK (resolved.chords && resolved.chords->size() == baseline.size());
            if (resolved.chords)
                for (std::size_t index = 0; index < baseline.size(); ++index)
                    CHECK ((*resolved.chords)[index].chordPitches == baseline[index]);
        }

    const auto withOffsets = parseProgression ("[1@+1], [2@+1]", cMajor);
    const auto withoutOffsets = parseProgression ("[1], [2]", cMajor);
    VoicingSettings voiceLed;
    const auto shifted = resolveProgression (*withOffsets.progression, voiceLed, 0);
    const auto baseline = resolveProgression (*withoutOffsets.progression, voiceLed, 0);
    const auto bassEnabled = resolveProgression (*withOffsets.progression, voiceLed, 1);
    CHECK (shifted.chords && baseline.chords && shifted.chords->size() == 2);
    if (shifted.chords && baseline.chords)
        for (std::size_t index = 0; index < 2; ++index)
        {
            CHECK ((*shifted.chords)[index].rootMidi == (*baseline.chords)[index].rootMidi + 12);
            CHECK ((*shifted.chords)[index].chordPitches.front()
                   == (*baseline.chords)[index].chordPitches.front() + 12);
        }
    CHECK (shifted.chords && bassEnabled.chords
           && shifted.chords->size() == bassEnabled.chords->size());
    if (shifted.chords && bassEnabled.chords)
        for (std::size_t index = 0; index < shifted.chords->size(); ++index)
            CHECK (((*shifted.chords)[index].chordPitches
                    == (*bassEnabled.chords)[index].chordPitches));
}

static void checkRootBassResolution()
{
    const Key cMajor {{'C', 0}, Mode::major};
    VoicingSettings rootPosition;
    rootPosition.mode = VoicingMode::rootPosition;
    const auto pinned = parseProgression ("[C5,E4,G4]:2", cMajor);
    for (int octaves = 1; octaves <= 3; ++octaves)
    {
        const auto resolved = resolveProgression (*pinned.progression, rootPosition, octaves);
        CHECK (resolved.chords && resolved.chords->size() == 1);
        if (resolved.chords)
        {
            const auto expectedBass = 72 - 12 * octaves;
            CHECK ((*resolved.chords)[0].bassMidi == std::optional<int> {expectedBass});
            if (octaves == 1)
                CHECK (((*resolved.chords)[0].pitches == std::vector<int> {60, 64, 67, 72}));
        }
    }

    const auto duplicateBass = parseProgression ("[C4,C3]", cMajor);
    const auto duplicateResult = resolveProgression (*duplicateBass.progression, rootPosition, 1);
    CHECK (duplicateResult.chords && duplicateResult.chords->size() == 1);
    if (duplicateResult.chords)
    {
        CHECK (((*duplicateResult.chords)[0].pitches == std::vector<int> {48, 60}));
        CHECK ((*duplicateResult.chords)[0].bassMidi == std::optional<int> {48});
    }

    const auto ordinary = parseProgression ("C/E, Fm7/C", cMajor);
    const auto ordinaryOff = resolveProgression (*ordinary.progression, rootPosition, 0);
    const auto ordinaryOn = resolveProgression (*ordinary.progression, rootPosition, 1);
    const auto ordinaryBaseline = voiceProgression (*ordinary.progression, rootPosition);
    CHECK (ordinaryOff.chords && ordinaryOn.chords
           && ordinaryOff.chords->size() == ordinaryBaseline.size());
    if (ordinaryOff.chords && ordinaryOn.chords)
        for (std::size_t index = 0; index < ordinaryBaseline.size(); ++index)
        {
            CHECK ((*ordinaryOff.chords)[index].chordPitches == ordinaryBaseline[index]);
            CHECK ((*ordinaryOn.chords)[index].chordPitches == ordinaryBaseline[index]);
        }
    CHECK (ordinaryOn.chords && (*ordinaryOn.chords)[0].bassMidi
           && (*ordinaryOn.chords)[0].bassMidi != std::optional<int> {ordinaryBaseline[0].front()});

    const auto exactBounds = parseProgression ("[C-1], [G9]", cMajor);
    const auto exactResult = resolveProgression (*exactBounds.progression, rootPosition, 0);
    CHECK ((exactResult.chords && (*exactResult.chords)[0].pitches == std::vector<int> {0}
           && (*exactResult.chords)[1].pitches == std::vector<int> {127}));

    const auto highRoot = parseProgression ("[G9,14]", cMajor);
    const auto highResult = resolveProgression (*highRoot.progression, rootPosition, 0);
    CHECK ((highResult.chords && (*highResult.chords)[0].rootMidi == 127
           && (*highResult.chords)[0].chordPitches == std::vector<int> {71, 127}));

    const auto empty = parseProgression ("", cMajor);
    const auto emptyResult = resolveProgression (*empty.progression, rootPosition, 0);
    const auto invalidBass = resolveProgression (*empty.progression, rootPosition, 4);
    CHECK (emptyResult.chords && emptyResult.chords->empty());
    CHECK (! invalidBass.chords && ! invalidBass.diagnostics.empty());
}

static void checkResolutionBoundsAndRenderPlan()
{
    const Key cMajor {{'C', 0}, Mode::major};
    VoicingSettings rootPosition;
    rootPosition.mode = VoicingMode::rootPosition;

    const auto bassOverflow = parseProgression ("[C-1]", cMajor);
    const auto bassError = resolveProgression (*bassOverflow.progression, rootPosition, 1);
    CHECK (! bassError.chords && ! bassError.diagnostics.empty());

    rootPosition.lowMidi = 120;
    rootPosition.highMidi = 127;
    const auto relativeOverflow = parseProgression ("[1@+1]", cMajor);
    const auto relativeError = resolveProgression (*relativeOverflow.progression, rootPosition, 0);
    CHECK (! relativeError.chords && ! relativeError.diagnostics.empty());
    if (! relativeError.diagnostics.empty())
        CHECK (relativeError.diagnostics[0].offset == 1);

    rootPosition.lowMidi = 48;
    rootPosition.highMidi = 84;
    const auto parsed = parseProgression ("[C5,E4,G4]:2", cMajor);
    const auto resolved = resolveProgression (*parsed.progression, rootPosition, 1);
    CHECK (resolved.chords && resolved.chords->size() == 1);
    if (resolved.chords)
    {
        const std::vector<std::vector<int>> voicings {(*resolved.chords)[0].pitches};
        const auto plan = buildRenderPlan (*parsed.progression, voicings, 83);
        CHECK (plan.lengthBeats == 2.0);
        CHECK (plan.events.size() == 8);
        const auto bytes = serialiseMidiFile (plan, MidiFileSettings {});
        const auto events = readNoteEvents (bytes);
        CHECK (events.size() == 8);
        CHECK (events.front().noteOn && events.front().pitch == 60
               && events.front().velocity == 83 && events.front().channel == 1);
        CHECK (! events[4].noteOn && events[4].tick == 1920);
    }
}

int main()
{
    checkCustomChordParsing();
    checkCustomGrammarDiagnostics();
    checkCustomChordResolution();
    checkRootBassResolution();
    checkResolutionBoundsAndRenderPlan();
    CHECK (static_cast<int> (Mode::major) == 0);
    CHECK (static_cast<int> (Mode::minor) == 1);
    CHECK (allModes().size() == 9);
    CHECK (parseModeName ("DORIAN") == std::optional<Mode> { Mode::dorian });
    CHECK (parseModeName ("ionian") == std::optional<Mode> { Mode::major });
    CHECK (parseModeName ("aeolian") == std::optional<Mode> { Mode::minor });
    CHECK (! parseModeName ("unknown"));
    const std::array<std::array<int, 7>, 9> expectedScales {{
        {{0,2,4,5,7,9,11}}, {{0,2,3,5,7,8,10}}, {{0,2,3,5,7,9,10}},
        {{0,1,3,5,7,8,10}}, {{0,2,4,6,7,9,11}}, {{0,2,4,5,7,9,10}},
        {{0,1,3,5,6,8,10}}, {{0,2,3,5,7,8,11}}, {{0,2,3,5,7,9,11}}
    }};
    for (std::size_t i = 0; i < allModes().size(); ++i)
    {
        const auto mode = allModes()[i];
        CHECK (modeIntervals (mode) == expectedScales[i]);
        CHECK (parseModeName (modeName (mode)) == std::optional<Mode> { mode });
        const auto notes = Key {{'C', 0}, mode}.scaleNotes();
        for (std::size_t degree = 0; degree < notes.size(); ++degree)
            CHECK ((notes[degree].semitone() + 12) % 12 == expectedScales[i][degree]);
    }
    CHECK ((Key {{'C', 0}, Mode::dorian}.degree (3).name() == "Eb"));
    CHECK ((Key {{'A', 0}, Mode::melodicMinor}.degree (6).name() == "F#"));
    CHECK ((Key {{'A', 0}, Mode::harmonicMinor}.degree (7).name() == "G#"));
    CHECK ((Key {{'D', -1}, Mode::lydian}.degree (4).name() == "G"));

    // Expected chord symbols are independent fixtures, not generated by the parser.
    const std::array<Mode, 7> relativeModes {Mode::major, Mode::dorian, Mode::phrygian,
        Mode::lydian, Mode::mixolydian, Mode::minor, Mode::locrian};
    const std::string relativeTonics = "CDEFGAB";
    const std::array<std::string, 7> triads {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
    const std::array<std::string, 7> sevenths {"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5"};
    for (std::size_t mode = 0; mode < relativeModes.size(); ++mode)
        for (int degree = 1; degree <= 7; ++degree)
        {
            const Key key {{relativeTonics[mode], 0}, relativeModes[mode]};
            const auto expected = (mode + static_cast<std::size_t> (degree - 1)) % 7;
            const auto triad = parseNumericDegree (std::to_string (degree), key, 0);
            const auto seventh = parseNumericDegree (std::to_string (degree) + "(7)", key, 0);
            CHECK (triad.chord.has_value());
            CHECK (seventh.chord.has_value());
            if (triad.chord && seventh.chord)
            {
                CHECK (Chord (*triad.chord).symbol() == triads[expected]);
                CHECK (Chord (*seventh.chord).symbol() == sevenths[expected]);
            }
        }
    const std::array<std::array<std::string, 7>, 4> minorChords {{
        {{"Am", "Bdim", "Caug", "Dm", "E", "F", "G#dim"}},
        {{"AmMaj7", "Bm7b5", "Cmaj7#5", "Dm7", "E7", "Fmaj7", "G#dim7"}},
        {{"Am", "Bm", "Caug", "D", "E", "F#dim", "G#dim"}},
        {{"AmMaj7", "Bm7", "Cmaj7#5", "D7", "E7", "F#m7b5", "G#m7b5"}}
    }};
    for (int variant = 0; variant < 4; ++variant)
        for (int degree = 1; degree <= 7; ++degree)
        {
            const Key key {{'A', 0}, variant < 2 ? Mode::harmonicMinor : Mode::melodicMinor};
            const auto token = std::to_string (degree) + (variant % 2 == 1 ? "(7)" : "");
            const auto parsed = parseNumericDegree (token, key, 0);
            CHECK (parsed.chord.has_value());
            if (parsed.chord)
                CHECK (Chord (*parsed.chord).symbol() == minorChords[variant][degree - 1]);
        }
    const Key numericCMajor {{'C', 0}, Mode::major};
    const std::array<std::pair<const char*, const char*>, 10> overrides {{
        {"2maj", "D"}, {"2m", "Dm"}, {"2maj7", "Dmaj7"}, {"2dom7", "D7"},
        {"2dim", "Ddim"}, {"2aug", "Daug"}, {"2m7b5", "Dm7b5"},
        {"2mMaj7", "DmMaj7"}, {"2dim7", "Ddim7"}, {"2augMaj7", "Dmaj7#5"}
    }};
    for (const auto& example : overrides)
    {
        const auto parsed = parseNumericDegree (example.first, numericCMajor, 0);
        CHECK (parsed.chord.has_value());
        if (parsed.chord) CHECK (Chord (*parsed.chord).symbol() == example.second);
    }
    for (const auto* invalid : {"0", "8", "9", "17", "27", "57", "01", "1645", "b7", "#4",
                              "2(9)", "2maj(7)", "2(7)m", "2/", "2(", "2)", "2foo", "2#5"})
    {
        const auto parsed = parseProgression (invalid, numericCMajor);
        CHECK (! parsed.progression);
        CHECK (! parsed.diagnostics.empty());
    }
    const auto badNumeric = parseProgression ("C, 1645", numericCMajor);
    CHECK (! badNumeric.progression);
    CHECK (badNumeric.diagnostics.size() == 1);
    if (! badNumeric.diagnostics.empty())
    {
        CHECK (badNumeric.diagnostics[0].offset == 3);
        CHECK (badNumeric.diagnostics[0].token == "1645");
    }
    const auto mixedNumeric = parseProgression ("1:8, vi7, F, 5(7)", numericCMajor);
    CHECK (mixedNumeric.progression.has_value());
    if (mixedNumeric.progression)
    {
        CHECK (mixedNumeric.progression->steps.size() == 4);
        CHECK (mixedNumeric.progression->totalBeats() == 20.0);
        CHECK (mixedNumeric.progression->steps[0].numericDegree == 1);
        CHECK (mixedNumeric.progression->steps[1].numericDegree == 0);
    }
    CHECK (parseProgression ("1-6-4-5", numericCMajor).progression->steps.size() == 4);
    const auto dorianMixed = parseProgression ("3, III, iii", Key {{'C', 0}, Mode::dorian});
    CHECK (dorianMixed.progression.has_value());
    if (dorianMixed.progression)
    {
        CHECK (dorianMixed.progression->steps[0].chord.symbol() == "Eb");
        CHECK (dorianMixed.progression->steps[1].chord.symbol() == "Eb");
        CHECK (dorianMixed.progression->steps[2].chord.symbol() == "Ebm");
    }
    const auto harmonicSeventh = parseNumericDegree ("3(7)", Key {{'A', 0}, Mode::harmonicMinor}, 0);
    if (harmonicSeventh.chord)
    {
        const auto tones = Chord (*harmonicSeventh.chord).tones();
        CHECK (tones.size() == 4);
        CHECK (tones[0].name() == "C" && tones[1].name() == "E"
               && tones[2].name() == "G#" && tones[3].name() == "B");
    }
    CHECK ((PitchClass {'C', 0}.semitone() == 0));
    CHECK ((PitchClass {'C', 1}.name() == "C#"));
    CHECK ((PitchClass {'G', 2}.name() == "G##"));
    CHECK ((Note {{'C', 0}, 4}.midi() == 60));
    CHECK ((Note {{'B', 0}, 3}.midi() == 59));

    CHECK ((Key {{'C', 0}, Mode::major}.degree (1).name() == "C"));
    CHECK ((Key {{'C', 0}, Mode::major}.degree (7).name() == "B"));
    CHECK ((Key {{'G', 1}, Mode::major}.degree (7).name() == "F##"));
    CHECK ((Key {{'A', 0}, Mode::minor}.degree (3).name() == "C"));
    CHECK ((Key {{'A', 0}, Mode::minor}.degree (7).name() == "G"));
    CHECK ((Key {{'E', -1}, Mode::major}.degree (4).name() == "Ab"));

    auto chord = [] (PitchClass root, std::string quality = "",
                     std::vector<std::pair<int,int>> alterations = {},
                     std::optional<PitchClass> bass = std::nullopt)
    {
        return Chord (ChordSpec { root, std::move (quality), std::move (alterations), bass, "" });
    };

    auto names = [] (const Chord& c)
    {
        std::string result;
        for (const auto& tone : c.tones())
            result += tone.name();
        return result;
    };

    CHECK (names (chord ({'C', 0}, "maj7")) == "CEGB");
    CHECK (names (chord ({'C', 0}, "maj9")) == "CEGBD");
    CHECK (names (chord ({'C', 0}, "6")) == "CEGA");
    CHECK (names (chord ({'C', 0}, "add9")) == "CEGD");
    CHECK (names (chord ({'C', 0}, "m7b5")) == "CEbGbBb");
    CHECK (names (chord ({'C', 0}, "dim7")) == "CEbGbBbb");
    CHECK (names (chord ({'G', 1}, "maj7")) == "G#B#D#F##");
    CHECK (names (chord ({'C', 0}, "7sus4")) == "CFGBb");
    CHECK (names (chord ({'C', 0}, "9sus4")) == "CFGBbD");
    CHECK (names (chord ({'C', 0}, "7", {{9, -1}})) == "CEGBbDb");
    CHECK (names (chord ({'C', 1}, "")) == "C#E#G#");
    CHECK (chord ({'C', 1}, "").symbol() == "C#");
    CHECK (chord ({'C', 0}, "7", {{9, -1}}).symbol() == "C7b9");
    CHECK (chord ({'C', 0}, "", {}, PitchClass {'E', 0}).symbol() == "C/E");

    CHECK (degreeLabel (chord ({'C', 0}, "maj7"), Key {{'C', 0}, Mode::major}) == "Imaj7");
    CHECK (degreeLabel (chord ({'A', 0}, "m7"), Key {{'C', 0}, Mode::major}) == "vi7");
    CHECK (degreeLabel (chord ({'C', 0}, "m"), Key {{'C', 0}, Mode::major}) == "i");
    CHECK (degreeLabel (chord ({'B', -1}, ""), Key {{'C', 0}, Mode::major}) == "bVII");

    CHECK (noteNameForPitch (chord ({'C', 1}, ""), 65) == "E#");
    CHECK (noteNameForPitch (chord ({'C', 1}, ""), 62) == "D");
    CHECK (notesForVoicing (chord ({'C', 0}, "maj7"), {48, 52, 55, 59}) == "C E G B");
    CHECK (notesForVoicing (chord ({'G', 0}, "7"), {62, 65, 67, 71}) == "D F G B");
    CHECK (notesForVoicing (chord ({'C', 0}, "", {}, PitchClass {'E', 0}),
                            {52, 60, 64, 67}) == "E C E G");

    auto parse = [] (std::string token)
    {
        return parseChordSymbol (token, 7);
    };

    CHECK (parse ("Cmaj7").chord->quality == "maj7");
    CHECK (parse ("CM7").chord->quality == "maj7");
    CHECK (parse ("Emin9").chord->quality == "m9");
    CHECK (parse ("Cm7b5").chord->quality == "m7b5");
    CHECK (parse ("Cø7").chord->quality == "m7b5");
    CHECK ((parse ("C7b9").chord->alterations == std::vector<std::pair<int,int>> {{9, -1}}));
    CHECK (parse ("C/E").chord->slashBass->name() == "E");
    CHECK (parse ("F#m7b5/C").chord->slashBass->name() == "C");
    CHECK (! parse ("C99").chord.has_value());
    CHECK (! parse ("C11").chord.has_value());
    CHECK (! parse ("C13").chord.has_value());
    CHECK (! parse ("C6/9").chord.has_value());
    CHECK (! parse ("H").chord.has_value());
    CHECK (parse ("H").error->offset == 7);
    CHECK (parse ("C99").error->message.find ("unknown extension") != std::string::npos);

    CHECK (parse ("Cmin").chord->quality == "m");
    CHECK (parse ("Co").chord->quality == "dim");
    CHECK (parse ("C°").chord->quality == "dim");
    CHECK (parse ("C+").chord->quality == "aug");
    CHECK (parse ("Cmin7").chord->quality == "m7");
    CHECK (parse ("CminMaj7").chord->quality == "mMaj7");
    CHECK (parse ("Co7").chord->quality == "dim7");
    CHECK (parse ("C°7").chord->quality == "dim7");
    CHECK (parse ("CM9").chord->quality == "maj9");
    CHECK (parse ("Cmin6").chord->quality == "m6");
    CHECK ((parse ("C7#9").chord->alterations == std::vector<std::pair<int,int>> {{9, 1}}));
    CHECK (! parse ("C##").chord.has_value());
    CHECK (parse ("C99").error->token == "C99");
    CHECK (parse ("C99").error->offset == 7);
    CHECK (parse ("F#m7b5/C").chord->root.name() == "F#");

    const Key cMajor {{'C', 0}, Mode::major};
    const Key aMinor {{'A', 0}, Mode::minor};

    auto progression = [] (std::string text, const Key& key)
    {
        return parseProgression (text, key);
    };

    auto symbols = [] (const Progression& p)
    {
        std::string result;
        for (const auto& step : p.steps)
            result += step.chord.symbol() + "|";
        return result;
    };

    CHECK (symbols (*progression ("C, Cmaj7, Dmaj7, Emin9, Fmaj7", cMajor).progression)
           == "C|Cmaj7|Dmaj7|Em9|Fmaj7|");
    CHECK (symbols (*progression ("I-IV-V", cMajor).progression) == "C|F|G|");
    CHECK (symbols (*progression ("i-iv-V", aMinor).progression) == "Am|Dm|E|");
    CHECK (symbols (*progression ("bVII, #iv", cMajor).progression) == "Bb|F#m|");
    CHECK (progression ("Cmaj7:2, G7", cMajor).progression->totalBeats() == 6.0);
    CHECK (progression ("C | Am | F | G", cMajor).progression->steps.size() == 4);
    CHECK (! progression ("C, H, C99", cMajor).progression.has_value());
    CHECK (progression ("C, H, C99", cMajor).diagnostics.size() == 2);
    CHECK (progression ("", cMajor).progression->steps.empty());
    CHECK (progression ("", cMajor).diagnostics.empty());
    CHECK (progression ("Cmaj7:0", cMajor).diagnostics.size() == 1);
    CHECK (progression ("Cmaj7:nan", cMajor).diagnostics.size() == 1);

    const VoicingSettings rootPosition {VoicingMode::rootPosition, 48, 84};
    const VoicingSettings voiceLed {VoicingMode::voiceLed, 48, 84};

    CHECK ((voiceChord (chord ({'C', 0}, "maj7"), std::nullopt, rootPosition)
           == std::vector<int> {48, 52, 55, 59}));
    CHECK ((voiceChord (chord ({'C', 0}, "", {}, PitchClass {'E', 0}), std::nullopt, rootPosition)
           == std::vector<int> {52, 60, 64, 67}));
    CHECK ((voiceChord (chord ({'G', 0}, "7"), std::vector<int> {60, 64, 67, 71}, voiceLed)
           == std::vector<int> {62, 65, 67, 71}));
    CHECK ((voiceChord (chord ({'G', 0}, "7"), std::vector<int> {60, 64, 67, 71}, voiceLed)
           != voiceChord (chord ({'G', 0}, "7"), std::vector<int> {60, 64, 67, 71}, rootPosition)));

    Progression p;
    p.steps.push_back ({chord ({'C', 0}, "maj7"), 4.0, "Cmaj7", 0});
    p.steps.push_back ({chord ({'G', 0}, "7"), 4.0, "G7", 0});

    auto rootVoicings = voiceProgression (p, rootPosition);
    auto ledVoicings = voiceProgression (p, voiceLed);
    CHECK ((rootVoicings == std::vector<std::vector<int>> {{48,52,55,59}, {55,59,62,65}}));
    CHECK (ledVoicings == rootVoicings);

    const auto plan = buildRenderPlan (p, rootVoicings, 100);
    CHECK (plan.lengthBeats == 8.0);
    CHECK (plan.events.size() == 16);
    CHECK (plan.events.front().beat == 0.0);
    CHECK (plan.events.front().noteOn);
    CHECK (! plan.events[4].noteOn);          // first event at beat 4 is a note-off
    CHECK (plan.events[4].beat == 4.0);
    CHECK (plan.events[8].noteOn);            // note-ons follow the note-offs
    CHECK (plan.events[8].beat == 4.0);
    CHECK (plan.events.back().beat == 8.0);
    CHECK (! plan.events.back().noteOn);

    RenderPlan midiPlan;
    midiPlan.lengthBeats = 4.0;
    midiPlan.events = {
        {4.0, 64, 0, 1, false, 0},
        {4.0, 60, 0, 1, false, 0},
        {0.0, 64, 100, 1, true, 0},
        {0.0, 60, 100, 1, true, 0},
    };

    const MidiFileSettings exportSettings;
    const auto bytes = serialiseMidiFile (midiPlan, exportSettings);
    CHECK (std::string (bytes.begin(), bytes.begin() + 4) == "MThd");
    CHECK (readU32 (bytes, 4) == 6);
    CHECK ((bytes[8] == 0 && bytes[9] == 0));                 // format 0
    CHECK ((bytes[10] == 0 && bytes[11] == 1));               // one track
    CHECK ((bytes[12] == 0x03 && bytes[13] == 0xC0));         // 960 PPQ
    CHECK (containsBytes (bytes, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20})); // 500000 us
    CHECK (containsBytes (bytes, {0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08}));
    CHECK (containsBytes (bytes, {0xFF, 0x2F, 0x00}));
    CHECK (bytes.back() == 0x00);
    CHECK (serialiseMidiFile (midiPlan, exportSettings) == bytes);

    const auto events = readNoteEvents (bytes);
    CHECK (events.size() == 4);
    CHECK (events[0].tick == 0 && events[0].noteOn && events[0].pitch == 60
           && events[0].velocity == 100 && events[0].channel == 1);
    CHECK (events[1].tick == 0 && events[1].noteOn && events[1].pitch == 64);
    CHECK (events[2].tick == 3840 && ! events[2].noteOn && events[2].pitch == 60);
    CHECK (events[3].tick == 3840 && ! events[3].noteOn && events[3].pitch == 64);

    auto dragSettings = exportSettings;
    dragSettings.includeTempoAndTimeSignature = false;
    const auto dragBytes = serialiseMidiFile (midiPlan, dragSettings);
    CHECK (! containsBytes (dragBytes, {0xFF, 0x51}));
    CHECK (! containsBytes (dragBytes, {0xFF, 0x58}));
    CHECK (containsBytes (dragBytes, {0xFF, 0x2F, 0x00}));

    const auto tempFile = std::filesystem::temp_directory_path()
                              / "chordtext-midi-writer-test.mid";
    std::filesystem::remove (tempFile);
    std::string writeError;
    CHECK (writeMidiFile (midiPlan, tempFile, exportSettings, writeError));
    CHECK (writeError.empty());
    CHECK (std::filesystem::exists (tempFile));
    std::filesystem::remove (tempFile);

    const auto notADirectory = std::filesystem::temp_directory_path()
                                  / "chordtext-midi-writer-blocker";
    std::filesystem::remove_all (notADirectory);
    {
        std::ofstream blockerFile (notADirectory);
        blockerFile << "x";
    }
    CHECK (! writeMidiFile (midiPlan, notADirectory / "out.mid", exportSettings, writeError));
    CHECK (! writeError.empty());
    std::filesystem::remove (notADirectory);

    return reportAndExit();
}
