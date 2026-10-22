#include "Theory/MidiFileWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace chordtext
{
namespace
{
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
}

std::vector<std::uint8_t> serialiseMidiFile (const RenderPlan& plan,
                                             const MidiFileSettings& settings)
{
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
            tempo = std::lround (60000000.0 / settings.bpm);
        tempo = std::clamp (tempo, 1L, 0xFFFFFFL);

        // Tempo meta event at tick 0.
        track.push_back (0x00);
        track.push_back (0xFF);
        track.push_back (0x51);
        track.push_back (0x03);
        track.push_back (static_cast<std::uint8_t> ((tempo >> 16) & 0xFF));
        track.push_back (static_cast<std::uint8_t> ((tempo >> 8) & 0xFF));
        track.push_back (static_cast<std::uint8_t> (tempo & 0xFF));

        // 4/4 time-signature meta event at tick 0.
        track.push_back (0x00);
        track.push_back (0xFF);
        track.push_back (0x58);
        track.push_back (0x04);
        track.push_back (0x04);
        track.push_back (0x02);
        track.push_back (0x18);
        track.push_back (0x08);
    }

    auto events = plan.events;
    std::stable_sort (events.begin(), events.end(),
                      [] (const MidiEvent& left, const MidiEvent& right)
                      {
                          if (left.beat != right.beat)
                              return left.beat < right.beat;

                          if (left.noteOn != right.noteOn)
                              return ! left.noteOn;

                          return left.pitch < right.pitch;
                      });

    std::int64_t previousTick = 0;
    for (const auto& event : events)
    {
        const auto tick = std::llround (event.beat * static_cast<double> (settings.ticksPerQuarterNote));
        appendVlq (track, static_cast<std::uint32_t> (tick - previousTick));
        previousTick = tick;

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

    std::vector<std::uint8_t> file;
    file.reserve (14 + 8 + track.size());

    file.insert (file.end(), {'M', 'T', 'h', 'd'});
    appendUint32 (file, 6);
    appendUint16 (file, 0);
    appendUint16 (file, 1);
    appendUint16 (file, static_cast<std::uint16_t> (settings.ticksPerQuarterNote));

    file.insert (file.end(), {'M', 'T', 'r', 'k'});
    appendUint32 (file, static_cast<std::uint32_t> (track.size()));
    file.insert (file.end(), track.begin(), track.end());

    return file;
}

bool writeMidiFile (const RenderPlan& plan,
                    const std::filesystem::path& file,
                    const MidiFileSettings& settings,
                    std::string& error)
{
    error.clear();

    const auto bytes = serialiseMidiFile (plan, settings);

    std::ofstream stream (file, std::ios::binary | std::ios::trunc);
    if (! stream.is_open())
    {
        error = "Cannot open " + file.u8string() + " for writing.";
        return false;
    }

    stream.write (reinterpret_cast<const char*> (bytes.data()),
                  static_cast<std::streamsize> (bytes.size()));

    if (! stream
        || stream.tellp() != std::streampos (static_cast<std::streamoff> (bytes.size())))
    {
        error = "Cannot write the MIDI file " + file.u8string() + ".";
        return false;
    }

    return true;
}
}
