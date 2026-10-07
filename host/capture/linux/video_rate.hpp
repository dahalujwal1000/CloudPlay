#pragma once

#include <spa/param/video/format.h>
#include <spa/pod/builder.h>

namespace cloudplay::capture {
inline void add_video_rate(spa_pod_builder &builder, const spa_fraction &rate, bool fixed,
                           unsigned maximum_fps = 60) {
    const spa_fraction minimum{0, 1};
    if (fixed)
        spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate), 0);
    else
        spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_framerate,
                            SPA_POD_CHOICE_RANGE_Fraction(&rate, &minimum, &rate), 0);
    // A nominal 60 Hz source may advertise 59.94 Hz. Cap, rather than require, 60.
    const spa_fraction minimum_maximum{1, 1};
    const spa_fraction maximum{maximum_fps, 1};
    spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_maxFramerate,
                        SPA_POD_CHOICE_RANGE_Fraction(&maximum, &minimum_maximum, &maximum), 0);
}
} // namespace cloudplay::capture
