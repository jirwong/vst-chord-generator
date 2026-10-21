#include "Check.h"
#include "Theory/Chord.h"
#include "Theory/Key.h"
#include "Theory/Note.h"

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

    return reportAndExit();
}
