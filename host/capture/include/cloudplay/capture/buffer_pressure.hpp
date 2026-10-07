#pragma once

#include <algorithm>
#include <cloudplay/capture/frame_capture.hpp>

namespace cloudplay::capture {

inline void buffer_added(FrameCaptureMetrics &metrics) noexcept {
    ++metrics.buffer_pool_size;
    metrics.max_buffer_pool_size = std::max(metrics.max_buffer_pool_size, metrics.buffer_pool_size);
}
inline void buffer_removed(FrameCaptureMetrics &metrics) noexcept {
    if (metrics.buffer_pool_size)
        --metrics.buffer_pool_size;
}
inline void buffer_acquired(FrameCaptureMetrics &metrics) noexcept {
    ++metrics.received;
    metrics.max_outstanding_buffers =
        std::max(metrics.max_outstanding_buffers, metrics.received - metrics.released);
}
inline void dequeue_batch(FrameCaptureMetrics &metrics, std::uint64_t count) noexcept {
    if (!count)
        return;
    ++metrics.dequeue_batches;
    if (count > 1)
        ++metrics.multi_dequeue_batches;
    metrics.max_dequeue_batch = std::max(metrics.max_dequeue_batch, count);
}

} // namespace cloudplay::capture
