#pragma once

#include "Theory/Key.h"
#include "Theory/Length.h"
#include "Theory/MidiFileWriter.h"
#include "Theory/Note.h"
#include "Theory/Voicer.h"

#include <optional>
#include <string>
#include <string_view>

namespace chordtext::cli
{
struct CliSettings
{
    PitchClass tonic { 'C', 0 };
    Mode mode = Mode::major;
    VoicingMode voicing = VoicingMode::voiceLed;
    int rangeIndex = 1;
    int velocity = 100;
    double bpm = 120.0;
    bool json = false;
    int bassOctaves = 0;
    LengthSpec defaultLength {};
    TimeSignature timeSignature {};

    Key key() const;
    VoicingSettings voicingSettings() const;
    TimingSettings timingSettings() const;
    MidiFileSettings midiFileSettings() const;
    std::string keyName() const;
    std::string modeName() const;
    std::string voicingName() const;
    std::string rangeName() const;
};

std::string formatSettingsLine (const CliSettings& settings);

std::optional<PitchClass> parseTonic (std::string_view text);
std::optional<Mode> parseMode (std::string_view text);
std::optional<VoicingMode> parseVoicing (std::string_view text);
std::optional<int> parseRangeIndex (std::string_view text);
std::optional<int> parseVelocity (std::string_view text);
std::optional<double> parseBpm (std::string_view text);
std::optional<int> parseBassOctaves (std::string_view text);
}
