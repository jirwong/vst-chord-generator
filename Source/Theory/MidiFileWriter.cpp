#include "Theory/MidiFileWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace chordtext
{
namespace
{
constexpr auto maximumVlq = std::uint32_t {0x0fffffffu};
constexpr auto maximumTicksPerQuarterNote = 0x7fff;

struct TimedMidiEvent
{
    MidiEvent event;
    std::int64_t tick = 0;
};

struct NoteKey
{
    int stepIndex = 0;
    int pitch = 0;
    int channel = 1;

    bool operator< (const NoteKey& other) const noexcept
    {
        if (stepIndex != other.stepIndex)
            return stepIndex < other.stepIndex;
        if (pitch != other.pitch)
            return pitch < other.pitch;
        return channel < other.channel;
    }
};

struct SerialisedFile
{
    std::vector<std::uint8_t> bytes;
    std::string error;
};

void appendUint16 (std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xFF));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xFF));
}

void appendUint32 (std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xFF));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xFF));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xFF));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xFF));
}

void appendVlq (std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    std::uint32_t buffer = value & 0x7F;
    while ((value >>= 7) != 0)
    {
        buffer <<= 8;
        buffer |= 0x80;
        buffer += value & 0x7F;
    }

    for (;;)
    {
        bytes.push_back (static_cast<std::uint8_t> (buffer & 0xFF));
        if ((buffer & 0x80) == 0)
            break;
        buffer >>= 8;
    }
}

int timeSignatureExponent (int denominator) noexcept
{
    auto exponent = 0;
    while (denominator > 1)
    {
        denominator >>= 1;
        ++exponent;
    }
    return exponent;
}

bool convertToTick (double beat, int ticksPerQuarterNote, std::int64_t& tick) noexcept
{
    if (! std::isfinite (beat) || beat < 0.0)
        return false;

    const auto scaled = beat * static_cast<double> (ticksPerQuarterNote);
    const auto largestSafeRounded
        = static_cast<double> (std::numeric_limits<std::int64_t>::max()) - 0.5;
    if (! std::isfinite (scaled) || scaled > largestSafeRounded)
        return false;

    tick = std::llround (scaled);
    return tick >= 0;
}

bool validateEvents (const RenderPlan& plan, const MidiFileSettings& settings,
                     std::vector<TimedMidiEvent>& result, std::string& error)
{
    result.clear();
    result.reserve (plan.events.size());

    for (const auto& event : plan.events)
    {
        std::int64_t tick = 0;
        if (! convertToTick (event.beat, settings.ticksPerQuarterNote, tick))
        {
            error = "MIDI event time is outside the file timing range.";
            return false;
        }

        if (event.pitch < 0 || event.pitch > 127 || event.channel < 1 || event.channel > 16
            || (event.noteOn && (event.velocity < 1 || event.velocity > 127)))
        {
            error = "MIDI event data is outside the supported range.";
            return false;
        }

        result.push_back ({event, tick});
    }

    auto chronological = result;
    std::stable_sort (chronological.begin(), chronological.end(),
                      [] (const TimedMidiEvent& left, const TimedMidiEvent& right)
                      {
                          if (left.event.beat != right.event.beat)
                              return left.event.beat < right.event.beat;

                          if (left.event.noteOn != right.event.noteOn)
                              return ! left.event.noteOn;

                          return left.event.pitch < right.event.pitch;
                      });

    std::map<NoteKey, TimedMidiEvent> activeNotes;
    for (const auto& timed : chronological)
    {
        const NoteKey key {timed.event.stepIndex, timed.event.pitch, timed.event.channel};
        if (timed.event.noteOn)
        {
            activeNotes[key] = timed;
            continue;
        }

        const auto active = activeNotes.find (key);
        if (active == activeNotes.end())
            continue;

        if (timed.event.beat > active->second.event.beat && timed.tick == active->second.tick)
        {
            error = "A positive note duration is smaller than one MIDI tick.";
            return false;
        }

        activeNotes.erase (active);
    }

    std::stable_sort (result.begin(), result.end(),
                      [] (const TimedMidiEvent& left, const TimedMidiEvent& right)
                      {
                          if (left.tick != right.tick)
                              return left.tick < right.tick;

                          if (left.event.noteOn != right.event.noteOn)
                              return ! left.event.noteOn;

                          return left.event.pitch < right.event.pitch;
                      });
    return true;
}
}

static SerialisedFile serialiseMidiFileChecked (const RenderPlan& plan,
                                                const MidiFileSettings& settings)
{
    SerialisedFile result;

    if (settings.ticksPerQuarterNote < 1
        || settings.ticksPerQuarterNote > maximumTicksPerQuarterNote)
    {
        result.error = "MIDI ticks per quarter note is outside the supported range.";
        return result;
    }

    if (settings.trackName.size() > maximumVlq)
    {
        result.error = "MIDI track name is too large.";
        return result;
    }

    if (settings.includeTempoAndTimeSignature && ! isValidTimeSignature (settings.signature))
    {
        result.error = "MIDI time signature is invalid.";
        return result;
    }

    std::vector<TimedMidiEvent> events;
    if (! validateEvents (plan, settings, events, result.error))
        return result;

    std::vector<std::uint8_t> track;

    // Track-name meta event at tick 0.
    track.push_back (0x00);
    track.push_back (0xFF);
    track.push_back (0x03);
    appendVlq (track, static_cast<std::uint32_t> (settings.trackName.size()));
    track.insert (track.end(), settings.trackName.begin(), settings.trackName.end());

    if (settings.includeTempoAndTimeSignature)
    {
        auto tempo = 500000L;
        if (std::isfinite (settings.bpm) && settings.bpm > 0.0)
        {
            const auto microseconds = 60000000.0 / settings.bpm;
            if (std::isfinite (microseconds))
            {
                if (microseconds <= 1.0)
                    tempo = 1L;
                else if (microseconds >= 0xFFFFFFL)
                    tempo = 0xFFFFFFL;
                else
                    tempo = std::lround (microseconds);
            }
        }

        // Tempo meta event at tick 0.
        track.push_back (0x00);
        track.push_back (0xFF);
        track.push_back (0x51);
        track.push_back (0x03);
        track.push_back (static_cast<std::uint8_t> ((tempo >> 16) & 0xFF));
        track.push_back (static_cast<std::uint8_t> ((tempo >> 8) & 0xFF));
        track.push_back (static_cast<std::uint8_t> (tempo & 0xFF));

        // MIDI stores the denominator as a base-two exponent.
        track.push_back (0x00);
        track.push_back (0xFF);
        track.push_back (0x58);
        track.push_back (0x04);
        track.push_back (static_cast<std::uint8_t> (settings.signature.numerator));
        track.push_back (static_cast<std::uint8_t> (
            timeSignatureExponent (settings.signature.denominator)));
        track.push_back (0x18);
        track.push_back (0x08);
    }

    std::int64_t previousTick = 0;
    for (const auto& timed : events)
    {
        const auto delta = timed.tick - previousTick;
        if (delta < 0 || delta > maximumVlq)
        {
            result.error = "MIDI event spacing is outside the variable-length timing range.";
            return result;
        }

        appendVlq (track, static_cast<std::uint32_t> (delta));
        previousTick = timed.tick;

        const auto& event = timed.event;
        const auto channelByte = static_cast<std::uint8_t> (event.channel - 1);
        track.push_back (static_cast<std::uint8_t> ((event.noteOn ? 0x90 : 0x80) | channelByte));
        track.push_back (static_cast<std::uint8_t> (event.pitch));
        track.push_back (static_cast<std::uint8_t> (event.noteOn ? event.velocity : 0));
    }

    // End-of-track meta event.
    track.push_back (0x00);
    track.push_back (0xFF);
    track.push_back (0x2F);
    track.push_back (0x00);

    if (track.size() > std::numeric_limits<std::uint32_t>::max())
    {
        result.error = "MIDI track data is too large.";
        return result;
    }

    result.bytes.reserve (14 + 8 + track.size());
    result.bytes.insert (result.bytes.end(), {'M', 'T', 'h', 'd'});
    appendUint32 (result.bytes, 6);
    appendUint16 (result.bytes, 0);
    appendUint16 (result.bytes, 1);
    appendUint16 (result.bytes, static_cast<std::uint16_t> (settings.ticksPerQuarterNote));

    result.bytes.insert (result.bytes.end(), {'M', 'T', 'r', 'k'});
    appendUint32 (result.bytes, static_cast<std::uint32_t> (track.size()));
    result.bytes.insert (result.bytes.end(), track.begin(), track.end());
    return result;
}

std::vector<std::uint8_t> serialiseMidiFile (const RenderPlan& plan,
                                             const MidiFileSettings& settings)
{
    return serialiseMidiFileChecked (plan, settings).bytes;
}

bool writeMidiFile (const RenderPlan& plan,
                    const std::filesystem::path& file,
                    const MidiFileSettings& settings,
                    std::string& error)
{
    error.clear();
    auto serialised = serialiseMidiFileChecked (plan, settings);
    if (serialised.bytes.empty())
    {
        error = serialised.error.empty() ? "Cannot serialise the MIDI file."
                                         : std::move (serialised.error);
        return false;
    }

    std::ofstream stream (file, std::ios::binary | std::ios::trunc);
    if (! stream.is_open())
    {
        error = "Cannot open " + file.u8string() + " for writing.";
        return false;
    }

    stream.write (reinterpret_cast<const char*> (serialised.bytes.data()),
                  static_cast<std::streamsize> (serialised.bytes.size()));

    if (! stream
        || stream.tellp() != std::streampos (static_cast<std::streamoff> (serialised.bytes.size())))
    {
        error = "Cannot write the MIDI file " + file.u8string() + ".";
        return false;
    }

    return true;
}
}
