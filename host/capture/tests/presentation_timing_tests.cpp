#include "cloudplay/capture/source_presentation_timing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace cloudplay::capture;
    const auto check = [](bool ok) {
        if (!ok)
            throw std::runtime_error("presentation timing assertion failed");
    };
    try {
        check(presentation_ns(0, 1, 25) == 1'000'000'025);
        check(!presentation_ns(0, 1, 1'000'000'000));
        check(!presentation_ns(UINT32_MAX, UINT32_MAX, 0));
        SourcePresentationTiming timing;
        check(timing.fps() == 0 && timing.span_seconds() == 0);
        check(timing.observe(0, 1, 0));
        check(timing.fps() == 0);
        check(timing.observe(0, 1, 16'666'667));
        check(std::abs(timing.fps() - 60) < 0.001);
        check(!timing.observe(0, 1, 16'666'667));
        check(!timing.observe(0, 0, 0));
        check(!timing.observe(0, 1, 1'000'000'000));
        check(timing.count == 2 && timing.invalid == 3);
        check(timing.observe(0, 2, 0));
        check(timing.span_seconds() == 1 && timing.fps() == 2);
        const auto stats = timing.intervals.summary();
        check(stats.count == 2 && stats.histogram[2] == 1 && stats.histogram[6] == 1);
        SourcePresentationTiming rollover;
        check(rollover.observe(0, UINT32_MAX, 999'999'999));
        check(rollover.observe(1, 0, 0));
        check(rollover.last - rollover.first == 1);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
