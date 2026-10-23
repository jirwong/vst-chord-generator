#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace chordscript
{
enum class PreviewMode { idle, momentary, held };

// Zero means no accepted gesture.
using PreviewGestureId = std::uint64_t;

struct PreviewRequest
{
    PreviewMode mode = PreviewMode::idle;
    int stepIndex = -1;
    PreviewGestureId gestureId = 0;
    std::uint64_t cancellationEpoch = 0;
    std::uint64_t dataRevision = 0;
    // Two bit masks covering pitches 0-63 and 64-127.
    std::array<std::uint64_t, 2> pitches {};
    int velocity = 100;
};

struct PreviewFeedback
{
    PreviewMode mode = PreviewMode::idle;
    int stepIndex = -1;
    PreviewGestureId gestureId = 0;
    std::uint64_t cancellationEpoch = 0;
    std::uint64_t dataRevision = 0;
};

// Copies the identity of the request that was actually applied. Feedback must
// not borrow current atomics: a cancellation racing with publication would
// otherwise stamp an old gesture as current.
PreviewFeedback makePreviewFeedback (const PreviewRequest& request) noexcept;

static_assert (std::is_trivially_copyable_v<PreviewRequest>);
static_assert (std::is_trivially_copyable_v<PreviewFeedback>);
static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

// Two independent single-producer/single-consumer triple buffers.
// The message thread publishes requests and reads feedback; the audio thread
// does the opposite. Payloads are fixed-size and own their data.
class ChordPreviewExchange
{
public:
    void publishRequest (const PreviewRequest& request) noexcept;
    bool readLatestRequest (PreviewRequest& request) noexcept;
    void publishFeedback (const PreviewFeedback& feedback) noexcept;
    bool readLatestFeedback (PreviewFeedback& feedback) noexcept;

private:
    template <typename Payload>
    struct Channel
    {
        std::array<Payload, 3> slots {};
        std::atomic<std::uint32_t> shared { 2 };
        int readIndex = 0;
        int writeIndex = 1;
    };

    Channel<PreviewRequest> requests;
    Channel<PreviewFeedback> feedback;
};
} // namespace chordscript
