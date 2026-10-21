#include "Theory/Chord.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace chordtext
{
namespace
{
constexpr std::array<char, 7> letters {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
constexpr std::array<int, 7> naturalSemitones {0, 2, 4, 5, 7, 9, 11};

struct Interval
{
    int semitones = 0;
    int degree = 1;
};

int letterIndex (char letter)
{
    for (int index = 0; index < static_cast<int> (letters.size()); ++index)
        if (letters[static_cast<std::size_t> (index)] == letter)
            return index;

    return 0;
}

int naturalSemitone (int letterOffset)
{
    const auto index = (letterOffset % static_cast<int> (letters.size()) + static_cast<int> (letters.size()))
                     % static_cast<int> (letters.size());
    return naturalSemitones[static_cast<std::size_t> (index)];
}

int majorIntervalForDegree (int degree)
{
    constexpr std::array<int, 7> steps {0, 2, 4, 5, 7, 9, 11};
    const auto zeroBased = degree - 1;
    const auto octave = zeroBased / 7;
    const auto step = zeroBased % 7;
    return 12 * octave + steps[static_cast<std::size_t> (step)];
}

std::vector<Interval> qualityIntervals (const std::string& quality)
{
    if (quality == "m")     return {{0, 1}, {3, 3}, {7, 5}};
    if (quality == "dim")   return {{0, 1}, {3, 3}, {6, 5}};
    if (quality == "aug")   return {{0, 1}, {4, 3}, {8, 5}};
    if (quality == "sus2")  return {{0, 1}, {2, 2}, {7, 5}};
    if (quality == "sus4")  return {{0, 1}, {5, 4}, {7, 5}};
    if (quality == "5")     return {{0, 1}, {7, 5}};
    if (quality == "6")     return {{0, 1}, {4, 3}, {7, 5}, {9, 6}};
    if (quality == "m6")    return {{0, 1}, {3, 3}, {7, 5}, {9, 6}};
    if (quality == "7")     return {{0, 1}, {4, 3}, {7, 5}, {10, 7}};
    if (quality == "maj7")  return {{0, 1}, {4, 3}, {7, 5}, {11, 7}};
    if (quality == "m7")    return {{0, 1}, {3, 3}, {7, 5}, {10, 7}};
    if (quality == "mMaj7") return {{0, 1}, {3, 3}, {7, 5}, {11, 7}};
    if (quality == "dim7")  return {{0, 1}, {3, 3}, {6, 5}, {9, 7}};
    if (quality == "m7b5")  return {{0, 1}, {3, 3}, {6, 5}, {10, 7}};
    if (quality == "7sus4") return {{0, 1}, {5, 4}, {7, 5}, {10, 7}};
    if (quality == "9")     return {{0, 1}, {4, 3}, {7, 5}, {10, 7}, {14, 9}};
    if (quality == "maj9")  return {{0, 1}, {4, 3}, {7, 5}, {11, 7}, {14, 9}};
    if (quality == "m9")    return {{0, 1}, {3, 3}, {7, 5}, {10, 7}, {14, 9}};
    if (quality == "add9")  return {{0, 1}, {4, 3}, {7, 5}, {14, 9}};
    if (quality == "9sus4") return {{0, 1}, {5, 4}, {7, 5}, {10, 7}, {14, 9}};

    return {{0, 1}, {4, 3}, {7, 5}};
}

std::vector<Interval> buildIntervals (const ChordSpec& spec)
{
    auto intervals = qualityIntervals (spec.quality);

    for (const auto& alteration : spec.alterations)
    {
        const auto degree = alteration.first;
        const auto delta = alteration.second;

        auto existing = std::find_if (intervals.begin(), intervals.end(),
                                      [degree] (const Interval& interval)
                                      {
                                          return interval.degree == degree;
                                      });

        if (existing != intervals.end())
            existing->semitones += delta;
        else
            intervals.push_back ({majorIntervalForDegree (degree) + delta, degree});
    }

    std::stable_sort (intervals.begin(), intervals.end(),
                      [] (const Interval& left, const Interval& right)
                      {
                          return left.semitones < right.semitones;
                      });

    return intervals;
}

PitchClass spell (PitchClass root, int semitones, int degree)
{
    const auto rootIndex = letterIndex (root.letter);
    const auto degreeIndex = (rootIndex + (degree - 1) % 7) % 7;
    const auto target = ((root.semitone() + semitones) % 12 + 12) % 12;
    auto alteration = (target - naturalSemitone (degreeIndex)) % 12;

    if (alteration > 6)
        alteration -= 12;
    else if (alteration < -6)
        alteration += 12;

    return {letters[static_cast<std::size_t> (degreeIndex)], alteration};
}

std::string alterationToken (const std::pair<int, int>& alteration)
{
    std::string result;
    const auto delta = alteration.second;

    if (delta > 0)
        result += std::string (static_cast<std::size_t> (delta), '#');
    else if (delta < 0)
        result += std::string (static_cast<std::size_t> (-delta), 'b');

    result += std::to_string (alteration.first);
    return result;
}

bool isLowercaseQuality (const std::string& quality)
{
    return quality == "m" || quality == "m6" || quality == "m7" || quality == "mMaj7"
        || quality == "m7b5" || quality == "m9"
        || quality == "dim" || quality == "dim7";
}

std::string qualitySuffix (const std::string& quality)
{
    if (quality == "" || quality == "m") return "";
    if (quality == "dim")   return "o";
    if (quality == "aug")   return "+";
    if (quality == "sus2")  return "sus2";
    if (quality == "sus4")  return "sus4";
    if (quality == "5")     return "5";
    if (quality == "6" || quality == "m6") return "6";
    if (quality == "7" || quality == "m7") return "7";
    if (quality == "maj7" || quality == "mMaj7") return "maj7";
    if (quality == "dim7")  return "o7";
    if (quality == "m7b5")  return "m7b5";
    if (quality == "7sus4") return "7sus4";
    if (quality == "9" || quality == "m9") return "9";
    if (quality == "add9")  return "add9";
    if (quality == "maj9")  return "maj9";
    if (quality == "9sus4") return "9sus4";

    return quality;
}
}

Chord::Chord (ChordSpec spec)
    : spec_ (std::move (spec))
{
    const auto intervals = buildIntervals (spec_);
    tones_.reserve (intervals.size());

    for (const auto& interval : intervals)
        tones_.push_back (spell (spec_.root, interval.semitones, interval.degree));
}

const ChordSpec& Chord::spec() const
{
    return spec_;
}

const std::vector<PitchClass>& Chord::tones() const
{
    return tones_;
}

std::string Chord::symbol() const
{
    auto result = spec_.root.name() + spec_.quality;

    for (const auto& alteration : spec_.alterations)
        result += alterationToken (alteration);

    if (spec_.slashBass.has_value())
        result += "/" + spec_.slashBass->name();

    return result;
}

std::string degreeLabel (const Chord& chord, const Key& key)
{
    const auto root = chord.spec().root;
    const auto tonic = key.tonic();
    const auto degree = (letterIndex (root.letter) - letterIndex (tonic.letter) + 7) % 7 + 1;

    const auto scaleDegree = key.degree (degree);
    const auto accidental = root.alteration - scaleDegree.alteration;

    std::string result;
    if (accidental > 0)
        result += std::string (static_cast<std::size_t> (accidental), '#');
    else if (accidental < 0)
        result += std::string (static_cast<std::size_t> (-accidental), 'b');

    static constexpr std::array<const char*, 7> romanNumerals
        {"I", "II", "III", "IV", "V", "VI", "VII"};

    auto numeral = std::string (romanNumerals[static_cast<std::size_t> (degree - 1)]);
    if (isLowercaseQuality (chord.spec().quality))
        for (auto& character : numeral)
            character = static_cast<char> (std::tolower (static_cast<unsigned char> (character)));

    result += numeral;
    result += qualitySuffix (chord.spec().quality);

    for (const auto& alteration : chord.spec().alterations)
        result += alterationToken (alteration);

    return result;
}
}
