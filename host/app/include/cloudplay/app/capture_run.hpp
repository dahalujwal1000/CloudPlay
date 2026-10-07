#pragma once

#include <chrono>
#include <cloudplay/capture/frame_capture.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>

namespace cloudplay::app {

// Diagnostic only: no encoder, transport, pixel reads or performance acceptance.
int run_capture(std::unique_ptr<capture::IFrameCapture> backend, std::chrono::milliseconds duration,
                std::ostream &output, const std::function<bool()> &cancel,
                std::uint32_t requested_runs = 1);

} // namespace cloudplay::app
