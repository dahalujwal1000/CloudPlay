#include "../linux/video_rate.hpp"
#include <cstdint>
#include <spa/pod/filter.h>
#include <spa/pod/iter.h>
#include <stdexcept>

int main() {
    {
        alignas(8) unsigned char storage[1024];
        auto builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        spa_pod_frame object;
        spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_Format,
                                    SPA_PARAM_EnumFormat);
        cloudplay::capture::add_video_rate(builder, spa_fraction{60, 1}, false, 61);
        const auto *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&builder, &object));
        const auto *maximum = spa_pod_find_prop(pod, nullptr, SPA_FORMAT_VIDEO_maxFramerate);
        const auto *nominal = spa_pod_find_prop(pod, nullptr, SPA_FORMAT_VIDEO_framerate);
        if (!maximum || !nominal || !spa_pod_is_choice(&maximum->value) ||
            !spa_pod_is_choice(&nominal->value))
            throw std::runtime_error("Missing diagnostic rate choices");
        const auto *cap = static_cast<const spa_fraction *>(
            SPA_POD_CHOICE_VALUES(reinterpret_cast<const spa_pod_choice *>(&maximum->value)));
        const auto *target = static_cast<const spa_fraction *>(
            SPA_POD_CHOICE_VALUES(reinterpret_cast<const spa_pod_choice *>(&nominal->value)));
        if (cap[0].num != 61 || cap[2].num != 61 || target[0].num != 60 || target[2].num != 60)
            throw std::runtime_error("Diagnostic cap changed the nominal target");
    }
    for (bool fixed : {false, true}) {
        alignas(8) unsigned char storage[1024];
        auto builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        spa_pod_frame object;
        spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_Format,
                                    SPA_PARAM_EnumFormat);
        cloudplay::capture::add_video_rate(builder, spa_fraction{60, 1}, fixed);
        const auto *pod = static_cast<spa_pod *>(spa_pod_builder_pop(&builder, &object));
        const auto *property = spa_pod_find_prop(pod, nullptr, SPA_FORMAT_VIDEO_framerate);
        const auto *maximum = spa_pod_find_prop(pod, nullptr, SPA_FORMAT_VIDEO_maxFramerate);
        if (!property || !maximum || !spa_pod_is_choice(&maximum->value))
            throw std::runtime_error("Missing frame rate parameters");
        const auto *max_choice = reinterpret_cast<const spa_pod_choice *>(&maximum->value);
        const auto *max_rate = static_cast<const spa_fraction *>(SPA_POD_CHOICE_VALUES(max_choice));
        if (max_choice->body.type != SPA_CHOICE_Range || max_rate[0].num != 60 ||
            max_rate[1].num != 1 || max_rate[2].num != 60 || max_rate[0].denom != 1 ||
            max_rate[1].denom != 1 || max_rate[2].denom != 1)
            throw std::runtime_error("Incorrect maximum rate");
        if (fixed) {
            if (!spa_pod_is_fraction(&property->value))
                throw std::runtime_error("Fixed rate remained a choice");
            const auto *rate =
                static_cast<const spa_fraction *>(SPA_POD_BODY_CONST(&property->value));
            if (rate->num != 60 || rate->denom != 1)
                throw std::runtime_error("Incorrect fixed rate");
        } else {
            const auto *choice = reinterpret_cast<const spa_pod_choice *>(&property->value);
            if (!spa_pod_is_choice(&property->value) || choice->body.type != SPA_CHOICE_Range)
                throw std::runtime_error("Baseline range changed");
            const auto *rates = static_cast<const spa_fraction *>(SPA_POD_CHOICE_VALUES(choice));
            if (rates[0].num != 60 || rates[1].num != 0 || rates[2].num != 60 ||
                rates[0].denom != 1 || rates[1].denom != 1 || rates[2].denom != 1)
                throw std::runtime_error("Incorrect baseline range bounds");
        }
        for (const auto source_max :
             {spa_fraction{60000, 1001}, spa_fraction{60, 1}, spa_fraction{144, 1}}) {
            alignas(8) unsigned char source_storage[1024], filtered_storage[2048];
            auto source_builder = SPA_POD_BUILDER_INIT(source_storage, sizeof(source_storage));
            spa_pod_frame source_object;
            spa_pod_builder_push_object(&source_builder, &source_object, SPA_TYPE_OBJECT_Format,
                                        SPA_PARAM_EnumFormat);
            const spa_fraction zero{0, 1}, minimum{1, 1};
            spa_pod_builder_add(&source_builder, SPA_FORMAT_VIDEO_framerate,
                                SPA_POD_Fraction(&zero), SPA_FORMAT_VIDEO_maxFramerate,
                                SPA_POD_CHOICE_RANGE_Fraction(&source_max, &minimum, &source_max),
                                0);
            const auto *source =
                static_cast<spa_pod *>(spa_pod_builder_pop(&source_builder, &source_object));
            auto filter_builder = SPA_POD_BUILDER_INIT(filtered_storage, sizeof(filtered_storage));
            spa_pod *filtered{};
            const int result = spa_pod_filter(&filter_builder, &filtered, source, pod);
            if (fixed) {
                if (result >= 0)
                    throw std::runtime_error("Exact rate matched a variable-rate-only producer");
                continue;
            }
            if (result < 0 || !filtered || spa_pod_fixate(filtered) < 0)
                throw std::runtime_error("Nominal refresh rate failed negotiation");
            const auto *negotiated =
                spa_pod_find_prop(filtered, nullptr, SPA_FORMAT_VIDEO_maxFramerate);
            if (!negotiated)
                throw std::runtime_error("Negotiated maximum missing");
            std::uint32_t count{}, choice{};
            const auto *value = spa_pod_get_values(&negotiated->value, &count, &choice);
            spa_fraction actual{};
            if (spa_pod_get_fraction(value, &actual) < 0 || actual.denom == 0 || actual.num == 0 ||
                actual.num > 60 * actual.denom ||
                static_cast<std::uint64_t>(actual.num) * source_max.denom >
                    static_cast<std::uint64_t>(source_max.num) * actual.denom)
                throw std::runtime_error("Negotiated maximum exceeds source or requested cap");
        }
    }
}
