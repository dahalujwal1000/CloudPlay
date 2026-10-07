#include <bit>
#include <cloudplay/input/packet.hpp>
#include <stdexcept>

namespace cloudplay::input {
namespace {
std::uint64_t integer(std::span<const std::uint8_t> bytes, std::size_t offset,
                      std::size_t size) noexcept {
    std::uint64_t result{};
    for (std::size_t i = 0; i < size; ++i)
        result = (result << 8) | bytes[offset + i];
    return result;
}
bool keyboard_usage(std::uint16_t usage) noexcept {
    return (usage >= 0x04 && usage <= 0x65) || (usage >= 0x67 && usage <= 0x73) ||
           (usage >= 0xe0 && usage <= 0xe7);
}
} // namespace

DecodeResult decode_packet(std::span<const std::uint8_t> bytes,
                           std::uint64_t expected_session) noexcept {
    if (bytes.size() < header_size || bytes.size() > maximum_packet_size)
        return DecodeError::Length;
    if (bytes[0] != 'C' || bytes[1] != 'P' || bytes[2] != 'I' || bytes[3] != 'N')
        return DecodeError::Magic;
    if (bytes[4] != 1)
        return DecodeError::Version;
    const auto type = bytes[5];
    if (type < 1 || type > 4)
        return DecodeError::EventType;
    const std::size_t payload_size = type == 3 ? 4 : 2;
    if (integer(bytes, 6, 2) != payload_size || bytes.size() != header_size + payload_size)
        return DecodeError::Length;
    const auto session = integer(bytes, 24, 8);
    if (!expected_session || !session || session != expected_session)
        return DecodeError::Session;
    const auto sequence = integer(bytes, 8, 8);
    if (!sequence)
        return DecodeError::Sequence;
    const auto timestamp = integer(bytes, 16, 8);
    if (!timestamp)
        return DecodeError::Timestamp;
    Payload payload;
    if (type == 1 || type == 2) {
        const auto usage = static_cast<std::uint16_t>(integer(bytes, 32, 2));
        if (!keyboard_usage(usage))
            return DecodeError::Payload;
        payload = KeyAction{usage, type == 1};
    } else if (type == 3) {
        const auto dx =
            std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(integer(bytes, 32, 2)));
        const auto dy =
            std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(integer(bytes, 34, 2)));
        if (dx < -4096 || dx > 4096 || dy < -4096 || dy > 4096)
            return DecodeError::Payload;
        payload = MouseMotion{dx, dy};
    } else {
        if (bytes[32] < 1 || bytes[32] > 5 || bytes[33] > 1)
            return DecodeError::Payload;
        payload = MouseButtonAction{bytes[32], bytes[33] == 1};
    }
    return Packet{sequence, timestamp, session, payload};
}

PacketReceiver::PacketReceiver(std::uint64_t authenticated_session)
    : session_id_(authenticated_session) {
    if (!session_id_)
        throw std::invalid_argument("An authenticated nonzero session binding is required");
}

DecodeResult PacketReceiver::receive(std::span<const std::uint8_t> bytes) noexcept {
    auto result = decode_packet(bytes, session_id_);
    if (const auto *packet = std::get_if<Packet>(&result)) {
        if (packet->sequence <= last_sequence_)
            return DecodeError::Replay;
        last_sequence_ = packet->sequence;
    }
    return result;
}
} // namespace cloudplay::input
