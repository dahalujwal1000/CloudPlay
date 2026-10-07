#include <cloudplay/input/dispatch.hpp>
#include <stdexcept>

namespace cloudplay::input {

Dispatcher::Dispatcher(std::uint64_t authenticated_session, IInputSink &sink, std::size_t capacity)
    : receiver_(authenticated_session), sink_(sink), capacity_(capacity) {
    if (!capacity_ || capacity_ > maximum_capacity)
        throw std::invalid_argument("Input queue capacity must be 1..64");
}
Dispatcher::~Dispatcher() { static_cast<void>(close()); }

EnqueueResult Dispatcher::enqueue(std::span<const std::uint8_t> bytes) noexcept {
    if (state_ == DispatchState::Dispatching || state_ == DispatchState::Closing)
        return EnqueueStatus::Busy;
    if (state_ != DispatchState::Open)
        return EnqueueStatus::Closed;
    auto decoded = receiver_.receive(bytes);
    if (const auto *error = std::get_if<DecodeError>(&decoded)) {
        ++diagnostics_.invalid;
        return *error;
    }
    if (size_ == capacity_) {
        ++diagnostics_.overflows;
        reason_ = CloseReason::Overflow;
        cleanup();
        return EnqueueStatus::Overflow;
    }
    queue_[(head_ + size_) % capacity_] = std::get<Packet>(decoded);
    ++size_;
    ++diagnostics_.accepted;
    return EnqueueStatus::Accepted;
}

std::size_t Dispatcher::drain(std::size_t maximum_events) noexcept {
    if (state_ != DispatchState::Open)
        return 0;
    state_ = DispatchState::Dispatching;
    std::size_t dispatched{};
    while (size_ && dispatched < maximum_events) {
        const auto payload = queue_[head_]->payload;
        queue_[head_].reset();
        head_ = (head_ + 1) % capacity_;
        --size_;
        // A failed press may already have reached the OS. Track it before applying.
        if (const auto *key = std::get_if<KeyAction>(&payload); key && key->pressed)
            keys_.set(key->usage);
        if (const auto *button = std::get_if<MouseButtonAction>(&payload);
            button && button->pressed)
            buttons_.set(button->button - 1);
        if (!sink_.apply(payload)) {
            ++diagnostics_.sink_failures;
            reason_ = CloseReason::SinkFailure;
            cleanup();
            return dispatched;
        }
        if (const auto *key = std::get_if<KeyAction>(&payload); key && !key->pressed)
            keys_.reset(key->usage);
        if (const auto *button = std::get_if<MouseButtonAction>(&payload);
            button && !button->pressed)
            buttons_.reset(button->button - 1);
        ++dispatched;
        ++diagnostics_.dispatched;
    }
    state_ = DispatchState::Open;
    return dispatched;
}

bool Dispatcher::cleanup() noexcept {
    state_ = DispatchState::Closing;
    diagnostics_.discarded_pending += size_;
    for (auto &entry : queue_)
        entry.reset();
    head_ = size_ = 0;
    for (std::size_t usage = 0; usage < keys_.size(); ++usage) {
        if (!keys_[usage])
            continue;
        if (sink_.apply(KeyAction{static_cast<std::uint16_t>(usage), false})) {
            keys_.reset(usage);
            ++diagnostics_.releases;
        } else
            ++diagnostics_.release_failures;
    }
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        if (!buttons_[button])
            continue;
        if (sink_.apply(MouseButtonAction{static_cast<std::uint8_t>(button + 1), false})) {
            buttons_.reset(button);
            ++diagnostics_.releases;
        } else
            ++diagnostics_.release_failures;
    }
    state_ = keys_.any() || buttons_.any() ? DispatchState::CleanupFailed : DispatchState::Closed;
    return state_ == DispatchState::Closed;
}

bool Dispatcher::close() noexcept {
    if (state_ == DispatchState::Dispatching || state_ == DispatchState::Closing)
        return false;
    if (state_ == DispatchState::Closed)
        return true;
    if (reason_ == CloseReason::None)
        reason_ = CloseReason::Requested;
    return cleanup();
}
} // namespace cloudplay::input
