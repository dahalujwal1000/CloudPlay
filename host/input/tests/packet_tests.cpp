#include <array>
#include <cloudplay/input/packet.hpp>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
using namespace cloudplay::input;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Input packet test failed");
}
void put(std::vector<std::uint8_t> &bytes, std::size_t offset, std::size_t length,
         std::uint64_t value) {
    for (std::size_t i = 0; i < length; ++i) {
        bytes[offset + length - 1 - i] = static_cast<std::uint8_t>(value & 0xff);
        value >>= 8;
    }
}
std::vector<std::uint8_t> packet(std::uint8_t type, std::uint64_t sequence = 1) {
    std::vector<std::uint8_t> bytes(type == 3 ? 36 : 34);
    bytes[0] = 'C';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'N';
    bytes[4] = 1;
    bytes[5] = type;
    put(bytes, 6, 2, bytes.size() - header_size);
    put(bytes, 8, 8, sequence);
    put(bytes, 16, 8, 0x0102030405060708ULL);
    put(bytes, 24, 8, 0xf1f2f3f4f5f6f7f8ULL);
    put(bytes, 32, 2, 0x1a);
    return bytes;
}
constexpr std::uint64_t session = 0xf1f2f3f4f5f6f7f8ULL;
void error(std::span<const std::uint8_t> bytes, DecodeError expected) {
    const auto result = decode_packet(bytes, session);
    check(std::holds_alternative<DecodeError>(result) && std::get<DecodeError>(result) == expected);
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<PacketReceiver>);
    const std::array<std::uint8_t, 34> golden{0x43, 0x50, 0x49, 0x4e, 1, 1, 0, 2, 0, 0,   0, 0,
                                              0,    0,    0,    1,    0, 0, 0, 0, 0, 0,   0, 1,
                                              0,    0,    0,    0,    0, 0, 0, 1, 0, 0x1a};
    const auto golden_packet = std::get<Packet>(decode_packet(golden, 1));
    check(golden_packet.sequence == 1 && golden_packet.timestamp_us == 1 &&
          golden_packet.session_id == 1 &&
          std::get<KeyAction>(golden_packet.payload).usage == 0x1a);
    auto bytes = packet(1, 0x1122334455667788ULL);
    auto result = decode_packet(bytes, session);
    check(std::holds_alternative<Packet>(result));
    const auto decoded = std::get<Packet>(result);
    check(decoded.sequence == 0x1122334455667788ULL &&
          decoded.timestamp_us == 0x0102030405060708ULL && decoded.session_id == session);
    check(std::get<KeyAction>(decoded.payload).usage == 0x1a &&
          std::get<KeyAction>(decoded.payload).pressed);
    result = decode_packet(packet(2), session);
    check(!std::get<KeyAction>(std::get<Packet>(result).payload).pressed);
    for (std::uint16_t usage : std::array<std::uint16_t, 6>{4, 0x65, 0x67, 0x73, 0xe0, 0xe7}) {
        bytes = packet(1);
        put(bytes, 32, 2, usage);
        check(std::holds_alternative<Packet>(decode_packet(bytes, session)));
    }
    for (std::uint16_t usage : std::array<std::uint16_t, 6>{0, 3, 0x66, 0x74, 0xdf, 0xe8}) {
        bytes = packet(1);
        put(bytes, 32, 2, usage);
        error(bytes, DecodeError::Payload);
    }
    bytes = packet(3);
    put(bytes, 32, 2, 0xf000);
    put(bytes, 34, 2, 0x1000);
    const auto motion =
        std::get<MouseMotion>(std::get<Packet>(decode_packet(bytes, session)).payload);
    check(motion.dx == -4096 && motion.dy == 4096);
    for (const auto value : {0xefff, 0x1001, 0x8000, 0x7fff}) {
        put(bytes, 32, 2, static_cast<std::uint64_t>(value));
        error(bytes, DecodeError::Payload);
    }
    bytes = packet(4);
    bytes[32] = 5;
    bytes[33] = 1;
    check(std::get<MouseButtonAction>(std::get<Packet>(decode_packet(bytes, session)).payload)
              .pressed);
    bytes[33] = 2;
    error(bytes, DecodeError::Payload);
    bytes[33] = 0;
    bytes[32] = 0;
    error(bytes, DecodeError::Payload);
    bytes[32] = 6;
    error(bytes, DecodeError::Payload);

    bytes = packet(1);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        error(std::span<const std::uint8_t>(bytes.data(), size), DecodeError::Length);
    auto oversized = bytes;
    oversized.resize(37);
    error(oversized, DecodeError::Length);
    auto trailing = bytes;
    trailing.push_back(0);
    error(trailing, DecodeError::Length);
    for (const auto &[offset, value, expected] : std::array{
             std::tuple{0U, 0U, DecodeError::Magic}, std::tuple{4U, 2U, DecodeError::Version},
             std::tuple{5U, 0U, DecodeError::EventType},
             std::tuple{5U, 255U, DecodeError::EventType},
             std::tuple{7U, 4U, DecodeError::Length}}) {
        auto bad = bytes;
        bad[offset] = static_cast<std::uint8_t>(value);
        error(bad, expected);
    }
    for (const auto &[offset, expected] :
         std::array{std::pair{8U, DecodeError::Sequence}, std::pair{16U, DecodeError::Timestamp},
                    std::pair{24U, DecodeError::Session}}) {
        auto bad = bytes;
        put(bad, offset, 8, 0);
        error(bad, expected);
    }
    check(std::get<DecodeError>(decode_packet(bytes, 0)) == DecodeError::Session);
    check(std::get<DecodeError>(decode_packet(bytes, session - 1)) == DecodeError::Session);
    PacketReceiver receiver(session);
    check(std::holds_alternative<Packet>(receiver.receive(bytes)) && receiver.last_sequence() == 1);
    check(std::get<DecodeError>(receiver.receive(bytes)) == DecodeError::Replay);
    auto invalid = packet(1, 99);
    invalid[4] = 2;
    check(std::get<DecodeError>(receiver.receive(invalid)) == DecodeError::Version);
    check(receiver.last_sequence() == 1);
    check(std::holds_alternative<Packet>(receiver.receive(packet(1, 2))));
    check(std::get<DecodeError>(receiver.receive(bytes)) == DecodeError::Replay);
    check(std::holds_alternative<Packet>(
        receiver.receive(packet(1, std::numeric_limits<std::uint64_t>::max()))));
    check(std::get<DecodeError>(receiver.receive(packet(1))) == DecodeError::Replay);
    try {
        PacketReceiver invalid_receiver(0);
        return 1;
    } catch (const std::invalid_argument &) {
    }

    // Deterministic malformed-input sweep, with no OS input effects.
    for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto mutation = bytes;
            mutation[offset] ^= static_cast<std::uint8_t>(1U << bit);
            static_cast<void>(decode_packet(mutation, session));
        }
    }
    std::uint32_t random = 0x12345678;
    for (int trial = 0; trial < 10000; ++trial) {
        random = random * 1664525U + 1013904223U;
        std::vector<std::uint8_t> noise(random % 80);
        for (auto &byte : noise) {
            random = random * 1664525U + 1013904223U;
            byte = static_cast<std::uint8_t>(random >> 24);
        }
        static_cast<void>(decode_packet(noise, session));
    }
}
