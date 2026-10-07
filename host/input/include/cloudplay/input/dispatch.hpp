#pragma once

#include <array>
#include <bitset>
#include <cloudplay/input/packet.hpp>
#include <optional>

namespace cloudplay::input {

// Must outlive Dispatcher. False may mean a partially applied operation.
// Releases must be idempotent. Never throw, reenter or destroy the dispatcher.
class IInputSink {
  public:
    virtual ~IInputSink() = default;
    virtual bool apply(const Payload &payload) noexcept = 0;
};
enum class DispatchState { Open, Dispatching, Closing, Closed, CleanupFailed };
enum class CloseReason { None, Requested, Overflow, SinkFailure };
enum class EnqueueStatus { Accepted, Overflow, Closed, Busy };
using EnqueueResult = std::variant<EnqueueStatus, DecodeError>;
struct DispatchDiagnostics {
    std::uint64_t accepted{}, dispatched{}, invalid{}, overflows{}, sink_failures{};
    std::uint64_t discarded_pending{}, releases{}, release_failures{};
};

// Owner-thread only; authenticated orchestration supplies session binding.
class Dispatcher final {
  public:
    static constexpr std::size_t maximum_capacity = 64;
    Dispatcher(std::uint64_t authenticated_session, IInputSink &sink,
               std::size_t capacity = maximum_capacity);
    ~Dispatcher();
    Dispatcher(const Dispatcher &) = delete;
    Dispatcher &operator=(const Dispatcher &) = delete;
    [[nodiscard]] EnqueueResult enqueue(std::span<const std::uint8_t> bytes) noexcept;
    std::size_t drain(std::size_t maximum_events = 8) noexcept;
    // Retry explicitly after CleanupFailed; returns false on reentry or failed release.
    bool close() noexcept;
    [[nodiscard]] DispatchState state() const noexcept { return state_; }
    [[nodiscard]] CloseReason close_reason() const noexcept { return reason_; }
    [[nodiscard]] std::size_t pending() const noexcept { return size_; }
    [[nodiscard]] std::size_t held_keys() const noexcept { return keys_.count(); }
    [[nodiscard]] std::size_t held_buttons() const noexcept { return buttons_.count(); }
    [[nodiscard]] DispatchDiagnostics diagnostics() const noexcept { return diagnostics_; }

  private:
    bool cleanup() noexcept;
    PacketReceiver receiver_;
    IInputSink &sink_;
    const std::size_t capacity_;
    std::array<std::optional<Packet>, maximum_capacity> queue_{};
    std::size_t head_{}, size_{};
    std::bitset<256> keys_;
    std::bitset<5> buttons_;
    DispatchState state_{DispatchState::Open};
    CloseReason reason_{CloseReason::None};
    DispatchDiagnostics diagnostics_;
};

} // namespace cloudplay::input
