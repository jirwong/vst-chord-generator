#include "Check.h"
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

    return reportAndExit();
}
