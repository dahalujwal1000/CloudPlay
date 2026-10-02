#include <chrono>
#include <cloudplay/telemetry/logger.hpp>

namespace cloudplay {

void Logger::transition(const Transition &transition) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const std::lock_guard lock(mutex_);
    output_ << "{\"timestampMs\":" << timestamp
            << ",\"component\":\"Host.App\",\"event\":\"session.transition\",\"from\":\""
            << name(transition.from) << "\",\"to\":\"" << name(transition.to) << "\",\"trigger\":\""
            << name(transition.event) << '"';
    if (transition.failure)
        output_ << ",\"failure\":\"" << name(*transition.failure) << '"';
    output_ << "}\n";
}

} // namespace cloudplay
