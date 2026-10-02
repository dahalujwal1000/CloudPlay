#include <cloudplay/capture/frame_policy.hpp>
#include <initializer_list>
#include <stdexcept>

int main() {
    using namespace cloudplay::capture;
    const FrameSize pool{1920, 1080};
    for (const auto size : {FrameSize{0, 0}, FrameSize{-1, 1080}, FrameSize{1920, 0}}) {
        if (decide_frame(pool, size) != FrameDecision::Discard)
            throw std::runtime_error("Invalid content delivered");
    }
    if (decide_frame(pool, pool) != FrameDecision::Deliver)
        throw std::runtime_error("Valid content discarded");
    for (const auto size : {FrameSize{1280, 720}, FrameSize{2560, 1440}, FrameSize{1920, 720}}) {
        if (decide_frame(pool, size) != FrameDecision::RecreatePool)
            throw std::runtime_error("Resize not detected");
    }
}
