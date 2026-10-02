#include <cloudplay/core/config.hpp>
#include <cloudplay/core/session.hpp>
#include <cloudplay/telemetry/logger.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition) {
    if (!condition)
        throw std::runtime_error("Test assertion failed");
}

void lifecycle() {
    using namespace cloudplay;
    Session session;
    require(!session.apply(SessionEvent::StreamReady));
    require(session.state() == SessionState::Offline);
    for (const auto event :
         {SessionEvent::Start, SessionEvent::Initialized, SessionEvent::Pair, SessionEvent::Paired,
          SessionEvent::Connect, SessionEvent::PeerConnected, SessionEvent::LaunchGame,
          SessionEvent::StreamReady}) {
        require(session.apply(event).has_value());
    }
    require(session.state() == SessionState::Streaming);
    require(!session.apply(SessionEvent::Fail));
    require(!session.apply(SessionEvent::Stop, SessionFailure::Network));
    require(session.state() == SessionState::Streaming);
    const auto failed = session.apply(SessionEvent::Fail, SessionFailure::Network);
    require(failed && failed->failure == SessionFailure::Network);
    require(session.state() == SessionState::Stopping);
    require(!session.apply(SessionEvent::Connect));
    require(session.apply(SessionEvent::Stopped).has_value());
    require(session.state() == SessionState::Offline);
    require(session.apply(SessionEvent::Start).has_value());
}

void recovery() {
    using namespace cloudplay;
    for (const auto state : {SessionState::Starting, SessionState::Ready, SessionState::Pairing,
                             SessionState::Connecting, SessionState::Connected,
                             SessionState::StartingGame, SessionState::Streaming}) {
        require(next_state(state, SessionEvent::Stop) == SessionState::Stopping);
        require(next_state(state, SessionEvent::Fail) == SessionState::Stopping);
    }
    require(next_state(SessionState::Streaming, SessionEvent::GameStopped) ==
            SessionState::Connected);
    require(next_state(SessionState::Streaming, SessionEvent::Disconnect) ==
            SessionState::Stopping);
    require(!next_state(SessionState::Offline, SessionEvent::Fail));
    require(!next_state(SessionState::Stopping, SessionEvent::Start));
}

void configuration() {
    cloudplay::StreamConfig config;
    require(!cloudplay::validate(config));
    for (const auto bitrate : {0U, 5U, 21U, 4294967295U}) {
        config.bitrate_mbps = bitrate;
        require(cloudplay::validate(config).has_value());
    }
    for (const auto bitrate : {6U, 12U, 20U}) {
        config.bitrate_mbps = bitrate;
        require(!cloudplay::validate(config));
    }
    config.fps = 0;
    require(cloudplay::validate(config).has_value());
}

void logging() {
    using namespace cloudplay;
    std::ostringstream output;
    Logger logger(output);
    logger.transition({SessionState::Streaming, SessionState::Stopping, SessionEvent::Fail,
                       SessionFailure::Capture});
    const auto record = output.str();
    require(record.starts_with("{\"timestampMs\":"));
    require(record.find("\"failure\":\"CAPTURE_FAILED\"") != std::string::npos);
    require(record.ends_with("}\n"));
}
} // namespace

int main() {
    try {
        lifecycle();
        recovery();
        configuration();
        logging();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Core lifecycle, recovery, configuration, and logging passed\n";
}
