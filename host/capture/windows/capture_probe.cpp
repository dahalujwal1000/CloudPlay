#include <cloudplay/capture/window_capture.hpp>
#include <winrt/base.h>
#include <charconv>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    std::uintptr_t handle{};
    unsigned seconds{5};
    if ((argc != 3 && argc != 5) || std::string_view(argv[1]) != "--window") {
        std::cerr << "Usage: cloudplay_capture_probe --window <decimal-or-0x-HWND> [--seconds 1..60]\n";
        return 2;
    }
    auto text = std::string_view(argv[2]);
    const bool hex = text.starts_with("0x");
    if (hex) text.remove_prefix(2);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), handle, hex ? 16 : 10);
    if (error != std::errc{} || end != text.data() + text.size() || handle == 0) return 2;
    if (argc == 5) {
        if (std::string_view(argv[3]) != "--seconds") return 2;
        const std::string_view duration(argv[4]);
        const auto [last, result] = std::from_chars(duration.data(), duration.data() + duration.size(), seconds);
        if (result != std::errc{} || last != duration.data() + duration.size() || seconds < 1 || seconds > 60)
            return 2;
    }
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        cloudplay::capture::WindowCapture capture;
        capture.start(reinterpret_cast<HWND>(handle));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            capture.poll([](const auto& frame) {
                if (!frame.texture) throw std::runtime_error("No GPU texture");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        capture.stop();
        const auto stats = capture.stats();
        std::cout << "{\"event\":\"capture.summary\",\"received\":" << stats.received
                  << ",\"delivered\":" << stats.delivered << ",\"discarded\":" << stats.discarded
                  << ",\"resizes\":" << stats.resizes << ",\"lastCaptureLatencyUs\":"
                  << stats.last_capture_latency_us << "}\n";
        return stats.delivered > 0 ? 0 : 3;
    } catch (const cloudplay::capture::CaptureError& error) {
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":" << static_cast<int>(error.reason)
                  << ",\"hresult\":" << error.result << "}\n";
        return 1;
    } catch (...) {
        std::cerr << "Capture probe failed\n";
        return 1;
    }
}
