#include <array>
#include <cloudplay/input/dispatch.hpp>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace cloudplay::input;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Input dispatch test failed");
}
std::vector<std::uint8_t> packet(std::uint8_t type, std::uint64_t sequence,
                                 std::uint16_t first = 0x1a, std::uint16_t second = 0) {
    std::vector<std::uint8_t> bytes(type == 3 ? 36 : 34);
    bytes[0] = 'C';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'N';
    bytes[4] = 1;
    bytes[5] = type;
    bytes[7] = type == 3 ? 4 : 2;
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[15 - i] = static_cast<std::uint8_t>(sequence & 0xff);
        sequence >>= 8;
    }
    bytes[23] = bytes[31] = 1;
    bytes[32] = static_cast<std::uint8_t>(first >> 8);
    bytes[33] = static_cast<std::uint8_t>(first & 0xff);
    if (type == 3) {
        bytes[34] = static_cast<std::uint8_t>(second >> 8);
        bytes[35] = static_cast<std::uint8_t>(second & 0xff);
    }
    return bytes;
}
class Sink final : public IInputSink {
  public:
    bool apply(const Payload &payload) noexcept override {
        if (count >= events.size())
            return false;
        events[count++] = payload;
        if (reenter) {
            reentry_ok = !reenter->close() && reenter->drain() == 0 &&
                         std::get<EnqueueStatus>(reenter->enqueue({})) == EnqueueStatus::Busy;
        }
        const auto *key = std::get_if<KeyAction>(&payload);
        const auto *button = std::get_if<MouseButtonAction>(&payload);
        const bool press = (key && key->pressed) || (button && button->pressed);
        const bool release = (key && !key->pressed) || (button && !button->pressed);
        if (press && fail_press_once) {
            fail_press_once = false;
            return false;
        }
        return !(release && (fail_release || (key && key->usage == fail_release_usage)));
    }
    std::array<Payload, 512> events;
    std::size_t count{};
    bool fail_press_once{}, fail_release{}, reentry_ok{};
    std::uint16_t fail_release_usage{};
    Dispatcher *reenter{};
};
void accepted(Dispatcher &dispatch, const std::vector<std::uint8_t> &bytes) {
    check(std::get<EnqueueStatus>(dispatch.enqueue(bytes)) == EnqueueStatus::Accepted);
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<Dispatcher>);
    Sink sink;
    Dispatcher dispatcher(1, sink, 4);
    accepted(dispatcher, packet(1, 1));
    accepted(dispatcher, packet(3, 2, 10, 0xffec));
    accepted(dispatcher, packet(3, 3, 5, 6));
    accepted(dispatcher, packet(2, 4));
    check(sink.count == 0 && dispatcher.pending() == 4 && dispatcher.held_keys() == 0);
    check(dispatcher.drain(0) == 0);
    check(dispatcher.drain(2) == 2 && dispatcher.pending() == 2 && dispatcher.held_keys() == 1);
    check(dispatcher.drain(2) == 2 && dispatcher.held_keys() == 0);
    check(std::get<MouseMotion>(sink.events[1]).dx == 10 &&
          std::get<MouseMotion>(sink.events[1]).dy == -20 &&
          std::get<MouseMotion>(sink.events[2]).dx == 5);
    accepted(dispatcher, packet(4, 5, 0x0101));
    sink.reenter = &dispatcher;
    check(dispatcher.drain() == 1 && dispatcher.held_buttons() == 1);
    check(sink.reentry_ok);
    check(dispatcher.close() && sink.reentry_ok && dispatcher.held_buttons() == 0);
    const auto closed_count = sink.count;
    check(dispatcher.close() && sink.count == closed_count);
    check(std::get<EnqueueStatus>(dispatcher.enqueue(packet(1, 6))) == EnqueueStatus::Closed);

    Sink overflow_sink;
    Dispatcher overflow(1, overflow_sink, 2);
    accepted(overflow, packet(1, 1));
    check(overflow.drain() == 1);
    accepted(overflow, packet(3, 2));
    accepted(overflow, packet(3, 3));
    check(std::get<DecodeError>(overflow.enqueue({})) == DecodeError::Length);
    check(overflow.state() == DispatchState::Open && overflow.pending() == 2);
    check(std::get<EnqueueStatus>(overflow.enqueue(packet(2, 4))) == EnqueueStatus::Overflow);
    check(overflow.state() == DispatchState::Closed && overflow.held_keys() == 0 &&
          overflow.close_reason() == CloseReason::Overflow && overflow_sink.count == 2);
    check(!std::get<KeyAction>(overflow_sink.events[1]).pressed);
    check(overflow.diagnostics().discarded_pending == 2 && overflow.diagnostics().overflows == 1);

    Sink partial_sink;
    partial_sink.fail_press_once = true;
    Dispatcher partial(1, partial_sink);
    accepted(partial, packet(1, 1));
    accepted(partial, packet(1, 2, 0x04));
    check(partial.drain() == 0 && partial.state() == DispatchState::Closed &&
          partial_sink.count == 2);
    check(partial.diagnostics().sink_failures == 1 && partial.diagnostics().releases == 1 &&
          partial.diagnostics().discarded_pending == 1 && partial.held_keys() == 0);

    Sink retry_sink;
    Dispatcher retry(1, retry_sink);
    accepted(retry, packet(1, 1));
    check(retry.drain() == 1);
    retry_sink.fail_release = true;
    accepted(retry, packet(2, 2));
    check(retry.drain() == 0 && retry.state() == DispatchState::CleanupFailed &&
          retry.held_keys() == 1);
    check(!retry.close() && retry.held_keys() == 1);
    retry_sink.fail_release = false;
    check(retry.close() && retry.held_keys() == 0 &&
          retry.close_reason() == CloseReason::SinkFailure);

    Sink mixed_sink;
    Dispatcher mixed(1, mixed_sink);
    accepted(mixed, packet(1, 1, 0x04));
    accepted(mixed, packet(1, 2, 0x05));
    accepted(mixed, packet(4, 3, 0x0101));
    check(mixed.drain() == 3);
    mixed_sink.fail_release_usage = 0x04;
    check(!mixed.close() && mixed.held_keys() == 1 && mixed.held_buttons() == 0);
    check(mixed.diagnostics().releases == 2 && mixed.diagnostics().release_failures == 1);
    check(std::get<EnqueueStatus>(mixed.enqueue(packet(1, 4))) == EnqueueStatus::Closed);
    mixed_sink.fail_release_usage = 0;
    check(mixed.close() && mixed.held_keys() == 0);

    Sink capacity_sink;
    Dispatcher full(1, capacity_sink);
    for (std::uint64_t sequence = 1; sequence <= Dispatcher::maximum_capacity; ++sequence)
        accepted(full, packet(3, sequence));
    check(full.pending() == Dispatcher::maximum_capacity);
    check(std::get<EnqueueStatus>(full.enqueue(packet(3, 65))) == EnqueueStatus::Overflow);
    check(full.pending() == 0 && full.diagnostics().discarded_pending == 64 &&
          capacity_sink.count == 0);

    Sink ring_sink;
    Dispatcher ring(1, ring_sink, 3);
    for (std::uint64_t sequence = 1; sequence <= 30; ++sequence) {
        accepted(ring, packet(3, sequence, static_cast<std::uint16_t>(sequence)));
        check(std::get<DecodeError>(ring.enqueue(packet(3, sequence))) == DecodeError::Replay);
        if (sequence % 3 == 0)
            check(ring.drain(3) == 3);
    }
    check(ring.pending() == 0 && ring_sink.count == 30 && ring.diagnostics().invalid == 30);
    for (std::size_t i = 0; i < ring_sink.count; ++i)
        check(std::get<MouseMotion>(ring_sink.events[i]).dx == static_cast<int>(i + 1));

    Sink destructor_sink;
    {
        Dispatcher active(1, destructor_sink);
        accepted(active, packet(1, 1));
        check(active.drain() == 1);
        accepted(active, packet(1, 2, 0x04));
    }
    check(destructor_sink.count == 2 && !std::get<KeyAction>(destructor_sink.events[1]).pressed);
    for (const auto capacity : {0U, 65U}) {
        try {
            Dispatcher invalid(1, sink, capacity);
            return 1;
        } catch (const std::invalid_argument &) {
        }
    }
}
