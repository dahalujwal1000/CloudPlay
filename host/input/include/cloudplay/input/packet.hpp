#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace cloudplay::input {

inline constexpr std::size_t header_size = 32;
inline constexpr std::size_t maximum_packet_size = 36;
struct KeyAction {
    std::uint16_t usage;
    bool pressed;
};
struct MouseMotion {
    std::int16_t dx, dy;
};
struct MouseButtonAction {
    std::uint8_t button;
    bool pressed;
};
using Payload = std::variant<KeyAction, MouseMotion, MouseButtonAction>;
struct Packet {
    std::uint64_t sequence, timestamp_us, session_id;
    Payload payload;
};
enum class DecodeError {
    Length,
    Magic,
    Version,
    EventType,
    Session,
    Sequence,
    Timestamp,
    Payload,
    Replay
};
using DecodeResult = std::variant<Packet, DecodeError>;

// Caller must authenticate independently; the ID is only session binding.
[[nodiscard]] DecodeResult decode_packet(std::span<const std::uint8_t> bytes,
                                         std::uint64_t expected_session) noexcept;

// Owner-thread receiver, no queues or OS input. Never reset a live session watermark.
class PacketReceiver final {
  public:
    explicit PacketReceiver(std::uint64_t authenticated_session);
    PacketReceiver(const PacketReceiver &) = delete;
    PacketReceiver &operator=(const PacketReceiver &) = delete;
    [[nodiscard]] DecodeResult receive(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] std::uint64_t last_sequence() const noexcept { return last_sequence_; }

  private:
    std::uint64_t session_id_;
    std::uint64_t last_sequence_{};
};

} // namespace cloudplay::input
