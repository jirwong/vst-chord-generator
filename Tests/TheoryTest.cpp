#include "Check.h"
#include "Theory/Chord.h"
#include "Theory/ChordSymbolParser.h"
#include "Theory/Key.h"
#include "Theory/Note.h"
#include "Theory/Progression.h"
#include "Theory/RenderPlan.h"
#include "Theory/Voicer.h"

using namespace chordtext;

int main()
{
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

    return reportAndExit();
}
