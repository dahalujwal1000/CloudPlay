#include <cloudplay/capture/frame_capture.hpp>
#include <stdexcept>
#include <type_traits>

int main() {
    using cloudplay::capture::FrameLease;
    static_assert(!std::is_copy_constructible_v<FrameLease>);
    static_assert(!std::is_move_constructible_v<FrameLease>);
    int released{};
    int buffer{};
    auto release = [](void *owner, void *resource) noexcept {
        if (resource)
            ++*static_cast<int *>(owner);
    };
    {
        const FrameLease lease(&released, &buffer, release);
        if (released != 0)
            throw std::runtime_error("Frame returned before consumer completed");
    }
    if (released != 1)
        throw std::runtime_error("Frame not returned exactly once");
    try {
        const FrameLease lease(&released, &buffer, release);
        throw std::runtime_error("Consumer failed");
    } catch (const std::runtime_error &) {
    }
    if (released != 2)
        throw std::runtime_error("Exception leaked a frame");
}
