#include <array>
#include <charconv>
#include <chrono>
#include <cloudplay/app/input_session.hpp>
#include <cloudplay/input/portal_input.hpp>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
std::array<std::uint8_t, 36> motion(std::uint64_t sequence, bool forward) {
    std::array<std::uint8_t, 36> bytes{};
    bytes[0] = 'C';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'N';
    bytes[4] = 1;
    bytes[5] = 3;
    bytes[7] = 4;
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[15 - i] = static_cast<std::uint8_t>(sequence & 0xff);
        sequence >>= 8;
    }
    bytes[23] = bytes[31] = 1;
    bytes[32] = bytes[34] = forward ? 0 : 0xff;
    bytes[33] = bytes[35] = forward ? 1 : 0xff;
    return bytes;
}
} // namespace

int main(int argc, char **argv) {
    using namespace cloudplay;
    using namespace cloudplay::input;
    unsigned seconds = 5;
    bool pointer_test{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--help") {
            std::cout << "Usage: cloudplay_linux_input_probe [--seconds 1..120] [--pointer-test]\n"
                         "Default: consent/lifecycle only, no input emitted.\n"
                         "--pointer-test: moves the cursor by alternating one-pixel deltas; no "
                         "keys/buttons.\n";
            return 0;
        }
        if (option == "--pointer-test") {
            pointer_test = true;
            continue;
        }
        if (option == "--seconds" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (error == std::errc{} && end == value.data() + value.size() && seconds >= 1 &&
                seconds <= 120)
                continue;
        }
        std::cerr << "Invalid input diagnostic option\n";
        return 2;
    }
    try {
        auto backend = std::make_unique<PortalInputSink>();
        auto *portal = backend.get();
        portal->start();
        // Local-only diagnostic binding, never a network authorization credential.
        app::InputSession input(1, std::move(backend));
        const auto begin = std::chrono::steady_clock::now();
        const auto deadline = begin + std::chrono::seconds(seconds);
        auto next_motion = begin;
        std::uint64_t sequence{};
        bool ok = true;
        while (std::chrono::steady_clock::now() < deadline) {
            portal->pump();
            if (portal->state() != PortalInputState::Active) {
                ok = false;
                break;
            }
            if (pointer_test && std::chrono::steady_clock::now() >= next_motion) {
                ++sequence;
                const auto packet = motion(sequence, sequence % 2 != 0);
                const auto admitted = input.receive(packet);
                const auto *status = std::get_if<EnqueueStatus>(&admitted);
                if (!status || *status != EnqueueStatus::Accepted || input.drain(1) != 1) {
                    ok = false;
                    break;
                }
                next_motion += std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ok = input.close() && ok;
        const auto metrics = input.diagnostics();
        portal->close();
        const auto native = portal->diagnostics();
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "{\"ok\":" << (ok ? "true" : "false") << ",\"seconds\":" << elapsed
                  << ",\"pointer_test\":" << (pointer_test ? "true" : "false")
                  << ",\"granted_devices\":" << native.granted_devices
                  << ",\"native_calls\":" << native.calls
                  << ",\"native_failures\":" << native.failures
                  << ",\"dispatched\":" << metrics.dispatched
                  << ",\"release_failures\":" << metrics.release_failures << "}\n";
        return ok && native.failures == 0 ? 0 : 1;
    } catch (const PortalInputError &error) {
        std::cerr << "{\"ok\":false,\"portal_failure\":" << static_cast<unsigned>(error.reason)
                  << "}\n";
        return 1;
    } catch (const std::exception &) {
        std::cerr << "Input diagnostic failed\n";
        return 1;
    }
}
