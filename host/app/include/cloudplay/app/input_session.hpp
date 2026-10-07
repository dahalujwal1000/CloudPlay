#pragma once

#include <cloudplay/core/session.hpp>
#include <cloudplay/input/dispatch.hpp>
#include <memory>

namespace cloudplay::app {

// Construct only after authorization. One owner thread and one peer session.
// The owned sink must not reenter or destroy this object from apply().
class InputSession final {
  public:
    InputSession(std::uint64_t authenticated_session, std::unique_ptr<input::IInputSink> sink,
                 std::size_t capacity = input::Dispatcher::maximum_capacity);
    InputSession(const InputSession &) = delete;
    InputSession &operator=(const InputSession &) = delete;

    [[nodiscard]] input::EnqueueResult receive(std::span<const std::uint8_t> bytes) noexcept;
    std::size_t drain(std::size_t maximum_events = 8) noexcept;
    // Orchestration forwards accepted Core transitions, not untrusted wire events.
    // False prevents declaring cleanup complete; retry close() explicitly.
    [[nodiscard]] bool on_transition(const Transition &transition) noexcept;
    [[nodiscard]] bool close() noexcept;
    [[nodiscard]] input::DispatchState state() const noexcept { return dispatcher_.state(); }
    [[nodiscard]] input::DispatchDiagnostics diagnostics() const noexcept {
        return dispatcher_.diagnostics();
    }

  private:
    // Reverse member destruction releases held controls before destroying the sink.
    std::unique_ptr<input::IInputSink> sink_;
    input::Dispatcher dispatcher_;
};

} // namespace cloudplay::app
