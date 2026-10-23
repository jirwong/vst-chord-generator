#include "ChordPreview.h"

namespace
{
constexpr std::uint32_t indexMask = 0x00ffffffu;
constexpr std::uint32_t freshBit = 0x01000000u;

template <typename Channel, typename Payload>
void publish (Channel& channel, const Payload& payload) noexcept
{
    // The writer owns writeIndex until the exchange hands it to the reader.
    channel.slots[static_cast<std::size_t> (channel.writeIndex)] = payload;
    const auto previous = channel.shared.exchange (
        static_cast<std::uint32_t> (channel.writeIndex) | freshBit,
        std::memory_order_acq_rel);
    channel.writeIndex = static_cast<int> (previous & indexMask);
}

template <typename Channel, typename Payload>
bool consume (Channel& channel, Payload& payload) noexcept
{
    // Only take part in an exchange once fresh data is visible. Exchanging on
    // a non-fresh mailbox would publish the reader's private slot and let the
    // writer end up owning the slot it just published as fresh.
    if ((channel.shared.load (std::memory_order_acquire) & freshBit) == 0)
        return false;

    const auto previous = channel.shared.exchange (
        static_cast<std::uint32_t> (channel.readIndex),
        std::memory_order_acq_rel);
    if ((previous & freshBit) == 0)
        return false;

    channel.readIndex = static_cast<int> (previous & indexMask);
    payload = channel.slots[static_cast<std::size_t> (channel.readIndex)];
    return true;
}
} // namespace

namespace chordscript
{
PreviewFeedback makePreviewFeedback (const PreviewRequest& request) noexcept
{
    PreviewFeedback feedback;
    feedback.mode = request.mode;
    feedback.stepIndex = request.stepIndex;
    feedback.gestureId = request.gestureId;
    feedback.cancellationEpoch = request.cancellationEpoch;
    feedback.dataRevision = request.dataRevision;
    return feedback;
}

void ChordPreviewExchange::publishRequest (const PreviewRequest& request) noexcept
{
    publish (requests, request);
}

bool ChordPreviewExchange::readLatestRequest (PreviewRequest& request) noexcept
{
    return consume (requests, request);
}

void ChordPreviewExchange::publishFeedback (const PreviewFeedback& value) noexcept
{
    publish (feedback, value);
}

bool ChordPreviewExchange::readLatestFeedback (PreviewFeedback& value) noexcept
{
    return consume (feedback, value);
}
} // namespace chordscript
