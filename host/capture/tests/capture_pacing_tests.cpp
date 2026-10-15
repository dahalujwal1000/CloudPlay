#include <cloudplay/capture/capture_pacing.hpp>
#include <limits>
#include <stdexcept>

int main() {
    using namespace cloudplay::capture;
    CapturePacingTrace trace;
    trace.observe({1, {}, {}, false, false});
    if (!trace.samples().empty() || trace.observed() != 1 || trace.overflow() != 1)
        throw std::runtime_error("Unprepared trace must not allocate in the callback");
    trace = {};
    trace.prepare();
    trace.observe({100, 90, 0, false, false});
    trace.observe({200, {}, {}, true, true});
    trace.observe({300, std::numeric_limits<std::int64_t>::min(),
                   std::numeric_limits<std::uint64_t>::max(), false, false});
    const auto samples = trace.samples();
    if (samples.size() != 3 || trace.overflow() || samples[0].arrival_ns != 100 ||
        samples[0].producer_pts_ns != 90 || samples[0].sequence != 0 ||
        samples[1].producer_pts_ns || samples[1].sequence || !samples[1].discontinuity ||
        !samples[1].corrupted ||
        samples[2].producer_pts_ns != std::numeric_limits<std::int64_t>::min() ||
        samples[2].sequence != std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Trace must preserve missing and invalid metadata for analysis");
    for (std::size_t i = 3; i < CapturePacingTrace::capacity + 10; ++i)
        trace.observe({static_cast<std::int64_t>(i), {}, {}, false, false});
    if (trace.samples().size() != CapturePacingTrace::capacity || trace.overflow() != 10 ||
        trace.observed() != CapturePacingTrace::capacity + 10 ||
        trace.samples()[0].arrival_ns != 100)
        throw std::runtime_error("Trace must bound storage and report omitted samples");
    trace = {};
    trace.prepare();
    if (trace.observed() || trace.overflow() || !trace.samples().empty())
        throw std::runtime_error("Restart must clear timing evidence");
}
