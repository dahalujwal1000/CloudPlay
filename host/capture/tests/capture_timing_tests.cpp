#include <cloudplay/capture/capture_timing.hpp>
#include <limits>
#include <stdexcept>

int main() {
    using namespace cloudplay::capture;
    TimingSamples samples(100);
    samples.prepare();
    samples.observe(-1);
    samples.observe(std::numeric_limits<double>::quiet_NaN());
    samples.observe(std::numeric_limits<double>::infinity());
    for (int i = 100; i >= 1; --i)
        samples.observe(i);
    auto result = samples.summary();
    if (result.count != 100 || result.overflow || result.mean_ms != 50.5 || result.min_ms != 1 ||
        result.max_ms != 100 || result.p50_ms != 50 || result.p95_ms != 95 || result.p99_ms != 99 ||
        result.histogram != std::array<std::uint64_t, 7>{7, 7, 3, 7, 10, 15, 51})
        throw std::runtime_error("Incorrect timing distribution");
    samples.observe(200);
    result = samples.summary();
    if (result.count != 101 || result.overflow != 1 || result.max_ms != 200 || result.p99_ms != 99)
        throw std::runtime_error("Timing overflow changed bounded percentile prefix");
    samples = TimingSamples(1);
    samples.prepare();
    samples.observe(16.5);
    result = samples.summary();
    if (result.count != 1 || result.overflow || result.p50_ms != 16.5 || result.p99_ms != 16.5)
        throw std::runtime_error("Restart did not reset timing samples");
    const auto empty = TimingSamples(0).summary();
    if (empty.count || empty.p50_ms || empty.mean_ms)
        throw std::runtime_error("Empty timing summary invalid");
    try {
        const CaptureStageTimer timer(&samples);
        throw std::runtime_error("consumer");
    } catch (const std::runtime_error &) {
    }
    if (samples.summary().count != 2)
        throw std::runtime_error("Exception path omitted duration");
}
