#pragma once

#include <cloudplay/input/dispatch.hpp>
#include <memory>
#include <stdexcept>

namespace cloudplay::input {

enum class PortalInputState { Idle, Starting, Active, Revoked, Failed, Closed };
enum class PortalInputFailure {
    None,
    SessionUnavailable,
    Unsupported,
    Denied,
    Timeout,
    Protocol,
    Call,
    Revoked
};
class PortalInputError final : public std::runtime_error {
  public:
    explicit PortalInputError(PortalInputFailure reason);
    const PortalInputFailure reason;
};
struct PortalInputDiagnostics {
    std::uint64_t calls{}, failures{}, invalid_payloads{}, suppressed_edges{};
    std::uint32_t granted_devices{};
};

// Owner-thread only. Start explicitly after peer authorization and local consent.
// No persistent permissions, screen capture, background thread or network endpoint.
class PortalInputSink final : public IInputSink {
  public:
    PortalInputSink();
    ~PortalInputSink() override;
    PortalInputSink(const PortalInputSink &) = delete;
    PortalInputSink &operator=(const PortalInputSink &) = delete;
    void start(unsigned consent_timeout_ms = 120000);
    void pump() noexcept;
    void close() noexcept;
    bool apply(const Payload &payload) noexcept override;
    [[nodiscard]] PortalInputState state() const noexcept;
    [[nodiscard]] PortalInputFailure last_failure() const noexcept;
    [[nodiscard]] PortalInputDiagnostics diagnostics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cloudplay::input
