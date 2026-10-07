#pragma once

#include <cloudplay/capture/capture_timing.hpp>
#include <cloudplay/capture/frame_capture.hpp>
#include <memory>

namespace cloudplay::capture {

class PipeWireCapture final : public IFrameCapture {
  public:
    PipeWireCapture();
    ~PipeWireCapture() override;
    PipeWireCapture(const PipeWireCapture &) = delete;
    PipeWireCapture &operator=(const PipeWireCapture &) = delete;
    void start(const CaptureOptions &options) override;
    bool poll(const Consumer &consume) override;
    void stop() override;
    [[nodiscard]] FrameCaptureState state() const override;
    [[nodiscard]] FrameCaptureMetrics metrics() const override;
    [[nodiscard]] std::array<TimingSummary, 9> timing_summary() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cloudplay::capture
