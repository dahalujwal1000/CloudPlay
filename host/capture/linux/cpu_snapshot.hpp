#pragma once

#include <cloudplay/capture/frame_capture.hpp>
#include <span>
#include <string>
#include <vector>

namespace cloudplay::capture {
struct CpuImage {
    std::uint32_t width{}, height{};
    std::vector<unsigned char> rgb;
};
// Exact byte interpretation only: no scaling, rotation or color-space conversion.
CpuImage unpack_linear_frame(const CapturedFrame &frame, std::span<const unsigned char> memory);
CpuImage read_cpu_snapshot(const CapturedFrame &frame);
void save_png(const CpuImage &image, const std::string &path);
CpuImage load_png(const std::string &path);
void log_buffer_diagnostics(const FrameBufferDiagnostics &layout);
} // namespace cloudplay::capture
