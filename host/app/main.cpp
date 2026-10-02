#include <charconv>
#include <cloudplay/core/config.hpp>
#include <cloudplay/core/session.hpp>
#include <cloudplay/telemetry/logger.hpp>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    cloudplay::StreamConfig config;
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--bitrate-mbps") {
            std::cerr << "Usage: cloudplay_host [--bitrate-mbps 6..20]\n";
            return 2;
        }
        const std::string_view value(argv[2]);
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), config.bitrate_mbps);
        if (error != std::errc{} || end != value.data() + value.size()) {
            std::cerr << "Invalid bitrate\n";
            return 2;
        }
    }
    if (const auto error = cloudplay::validate(config)) {
        std::cerr << *error << '\n';
        return 2;
    }

    cloudplay::Session session;
    cloudplay::Logger logger(std::cout);
    // Foundation smoke lifecycle only; no capture, encoder, network, or game is started.
    for (const auto event : {cloudplay::SessionEvent::Start, cloudplay::SessionEvent::Initialized,
                             cloudplay::SessionEvent::Stop, cloudplay::SessionEvent::Stopped}) {
        const auto transition = session.apply(event);
        if (!transition)
            return 1;
        logger.transition(*transition);
    }
    return 0;
}
