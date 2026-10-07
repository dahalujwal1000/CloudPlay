#include <cloudplay/app/input_session.hpp>
#include <stdexcept>
#include <utility>

namespace cloudplay::app {
namespace {
input::IInputSink &require_sink(const std::unique_ptr<input::IInputSink> &sink) {
    if (!sink)
        throw std::invalid_argument("Input session requires a sink");
    return *sink;
}
} // namespace

InputSession::InputSession(std::uint64_t authenticated_session,
                           std::unique_ptr<input::IInputSink> sink, std::size_t capacity)
    : sink_(std::move(sink)), dispatcher_(authenticated_session, require_sink(sink_), capacity) {}

input::EnqueueResult InputSession::receive(std::span<const std::uint8_t> bytes) noexcept {
    return dispatcher_.enqueue(bytes);
}

std::size_t InputSession::drain(std::size_t maximum_events) noexcept {
    return dispatcher_.drain(maximum_events);
}

bool InputSession::on_transition(const Transition &transition) noexcept {
    if (transition.to == SessionState::Stopping || transition.to == SessionState::Offline ||
        transition.event == SessionEvent::GameStopped)
        return close();
    return dispatcher_.state() == input::DispatchState::Open;
}

bool InputSession::close() noexcept { return dispatcher_.close(); }

} // namespace cloudplay::app
