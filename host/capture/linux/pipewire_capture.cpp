#include "gpu_import.hpp"
#include "portal_session.hpp"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cloudplay/capture/pipewire_capture.hpp>
#include <cloudplay/capture/presentation_timing.hpp>
#include <drm_fourcc.h>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <pipewire/pipewire.h>
#include <poll.h>
#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace cloudplay::capture {
namespace {
struct PixelFormat {
    spa_video_format spa;
    std::uint32_t drm;
};
constexpr PixelFormat formats[]{{SPA_VIDEO_FORMAT_BGRx, DRM_FORMAT_XRGB8888},
                                {SPA_VIDEO_FORMAT_BGRA, DRM_FORMAT_ARGB8888},
                                {SPA_VIDEO_FORMAT_RGBx, DRM_FORMAT_XBGR8888},
                                {SPA_VIDEO_FORMAT_RGBA, DRM_FORMAT_ABGR8888}};
std::uint32_t drm_format(spa_video_format format) {
    for (const auto &entry : formats)
        if (entry.spa == format)
            return entry.drm;
    return 0;
}
} // namespace

struct PipeWireCapture::Impl {
    const std::thread::id owner{std::this_thread::get_id()};
    FrameCaptureState state{FrameCaptureState::Idle};
    FrameCaptureMetrics metrics;
    CaptureOptions options;
    std::unique_ptr<PortalSession> portal;
    std::unique_ptr<GpuImporter> gpu;
    pw_main_loop *loop{};
    pw_context *context{};
    pw_core *core{};
    pw_stream *stream{};
    spa_hook listener{};
    spa_hook core_listener{};
    spa_video_info_raw format{};
    std::optional<FrameCaptureFailure> failure;
    const char *failure_stage{"pipewire.capture"};
    const Consumer *consumer{};
    std::exception_ptr consumer_error;
    std::optional<std::uint64_t> last_sequence;
    PresentationTiming presentation_timing;
    std::chrono::steady_clock::time_point deadline;

    void check() const {
        if (owner != std::this_thread::get_id())
            throw std::logic_error("Capture called from non-owner thread");
    }
    void fail(FrameCaptureFailure reason, const char *stage = "pipewire.capture") noexcept {
        if (!failure) {
            failure = reason;
            failure_stage = stage;
        }
        state = FrameCaptureState::Failed;
    }
    void cleanup() noexcept {
        if (stream) {
            spa_hook_remove(&listener);
            pw_stream_destroy(stream);
            stream = nullptr;
        }
        if (core) {
            spa_hook_remove(&core_listener);
            pw_core_disconnect(core);
            core = nullptr;
        }
        if (context) {
            pw_context_destroy(context);
            context = nullptr;
        }
        if (loop) {
            pw_main_loop_destroy(loop);
            loop = nullptr;
        }
        gpu.reset();
        portal.reset();
    }
    void release_buffer(pw_buffer *buffer) noexcept {
        const int result = pw_stream_queue_buffer(stream, buffer);
        if (result < 0) {
            metrics.native_error = result;
            fail(FrameCaptureFailure::PipeWire, "pipewire.return_buffer");
            return;
        }
        ++metrics.released;
        if (options.capture_diagnostics)
            std::cout << "{\"event\":\"capture.release\",\"released\":" << metrics.released
                      << ",\"received\":" << metrics.received << "}\n";
    }
    void update_format(const spa_pod *param) {
        if (!param)
            return;
        if (spa_format_video_raw_parse(param, &format) < 0) {
            fail(FrameCaptureFailure::UnsupportedFormat);
            return;
        }
        metrics.width = format.size.width;
        metrics.height = format.size.height;
        metrics.drm_format = drm_format(format.format);
        metrics.modifier = format.modifier;
        metrics.buffer.spa_format = static_cast<std::uint32_t>(format.format);
        metrics.negotiated_fps =
            format.framerate.denom
                ? static_cast<double>(format.framerate.num) / format.framerate.denom
                : 0;
        metrics.negotiated_max_fps =
            format.max_framerate.denom
                ? static_cast<double>(format.max_framerate.num) / format.max_framerate.denom
                : 0;
        if (!drm_format(format.format) || format.size.width != options.width ||
            format.size.height != options.height ||
            (!options.cpu_capture && !(format.flags & SPA_VIDEO_FLAG_MODIFIER)) ||
            (options.cpu_capture && (format.flags & SPA_VIDEO_FLAG_MODIFIER))) {
            fail(FrameCaptureFailure::UnsupportedFormat);
            return;
        }
        metrics.width = format.size.width;
        metrics.height = format.size.height;
        metrics.drm_format = drm_format(format.format);
        metrics.modifier = format.modifier;
        std::uint8_t storage[1024];
        auto builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        const spa_pod *params[4];
        params[0] = static_cast<spa_pod *>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
            SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_dataType,
            SPA_POD_CHOICE_FLAGS_Int(options.cpu_capture
                                         ? (1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)
                                         : (1 << SPA_DATA_DmaBuf))));
        params[1] = static_cast<spa_pod *>(
            spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                                       SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
                                       SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))));
        params[2] = static_cast<spa_pod *>(
            spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                                       SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
                                       SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_region))));
        params[3] = static_cast<spa_pod *>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type,
            SPA_POD_Id(SPA_META_VideoTransform), SPA_PARAM_META_size,
            SPA_POD_Int(sizeof(spa_meta_videotransform))));
        if (pw_stream_update_params(stream, params, 4) < 0)
            fail(FrameCaptureFailure::PipeWire);
    }
    void process() {
        // Bounded draining: keep only the newest of at most eight available buffers.
        pw_buffer *latest{};
        for (unsigned i = 0; i < 8; ++i) {
            auto *next = pw_stream_dequeue_buffer(stream);
            if (!next)
                break;
            ++metrics.received;
            if (options.capture_diagnostics)
                std::cout << "{\"event\":\"capture.acquire\",\"received\":" << metrics.received
                          << ",\"released\":" << metrics.released << "}\n";
            if (latest) {
                release_buffer(latest);
                ++metrics.discarded;
            }
            latest = next;
        }
        if (!latest)
            return;
        const FrameLease lease(this, latest, [](void *owner, void *buffer) noexcept {
            static_cast<Impl *>(owner)->release_buffer(static_cast<pw_buffer *>(buffer));
        });
        if (!consumer || failure || !metrics.drm_format) {
            ++metrics.discarded;
            return;
        }
        auto *buffer = latest->buffer;
        metrics.buffer = {};
        metrics.buffer.spa_format = static_cast<std::uint32_t>(format.format);
        metrics.buffer.plane_count = buffer->n_datas;
        for (std::uint32_t i = 0; i < std::min(buffer->n_datas, 4U); ++i) {
            const auto &data = buffer->datas[i];
            metrics.buffer.memory_types[i] = data.type;
            metrics.buffer.fds[i] = data.fd;
            metrics.buffer.data_flags[i] = data.flags;
            metrics.buffer.map_offsets[i] = data.mapoffset;
            metrics.buffer.max_sizes[i] = data.maxsize;
            if (data.chunk) {
                metrics.buffer.chunk_offsets[i] = data.chunk->offset;
                metrics.buffer.chunk_sizes[i] = data.chunk->size;
                metrics.buffer.strides[i] = data.chunk->stride;
            }
        }
        if (const auto *crop = static_cast<spa_meta_region *>(
                spa_buffer_find_meta_data(buffer, SPA_META_VideoCrop, sizeof(spa_meta_region)))) {
            metrics.buffer.crop_present = true;
            metrics.buffer.crop_x = crop->region.position.x;
            metrics.buffer.crop_y = crop->region.position.y;
            metrics.buffer.crop_width = crop->region.size.width;
            metrics.buffer.crop_height = crop->region.size.height;
        }
        if (const auto *transform =
                static_cast<spa_meta_videotransform *>(spa_buffer_find_meta_data(
                    buffer, SPA_META_VideoTransform, sizeof(spa_meta_videotransform)))) {
            metrics.buffer.transform_present = true;
            metrics.buffer.transform = transform->transform;
        }
#ifdef CLOUDPLAY_HAVE_SPA_SYNC_TIMELINE
        metrics.buffer.explicit_sync_present =
            spa_buffer_find_meta_data(buffer, SPA_META_SyncTimeline,
                                      sizeof(spa_meta_sync_timeline)) != nullptr;
        if (metrics.buffer.explicit_sync_present) {
            fail(FrameCaptureFailure::UnsupportedFormat, "pipewire.explicit_sync_not_supported");
            return;
        }
#endif
        if (buffer->n_datas < 1 || buffer->n_datas > 4) {
            ++metrics.discarded;
            fail(FrameCaptureFailure::UnsupportedFormat);
            return;
        }
        CapturedFrame frame;
        frame.width = format.size.width;
        frame.height = format.size.height;
        frame.drm_format = metrics.drm_format;
        frame.modifier = format.modifier;
        frame.plane_count = buffer->n_datas;
        frame.storage = options.cpu_capture ? FrameStorage::CpuMemory : FrameStorage::DmaBuf;
        if (options.cpu_capture && buffer->n_datas != 1) {
            fail(FrameCaptureFailure::UnsupportedFormat, "pipewire.cpu_multiplane_not_supported");
            return;
        }
        for (std::uint32_t i = 0; i < buffer->n_datas; ++i) {
            const auto &plane = buffer->datas[i];
            if ((!options.cpu_capture && plane.type != SPA_DATA_DmaBuf) ||
                (options.cpu_capture && plane.type != SPA_DATA_MemFd &&
                 plane.type != SPA_DATA_MemPtr)) {
                ++metrics.cpu_frames;
                ++metrics.discarded;
                fail(FrameCaptureFailure::UnsupportedFormat, "pipewire.expected_dmabuf");
                return;
            }
            const auto chunk_offset = !plane.chunk    ? 0U
                                      : plane.maxsize ? plane.chunk->offset % plane.maxsize
                                                      : plane.chunk->offset;
            if ((!options.cpu_capture && plane.fd < 0) || plane.fd > INT_MAX || !plane.chunk ||
                plane.chunk->stride <= 0 || plane.mapoffset > static_cast<std::uint32_t>(INT_MAX) ||
                chunk_offset > static_cast<std::uint32_t>(INT_MAX) - plane.mapoffset ||
                (plane.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED)) {
                ++metrics.discarded;
                fail(FrameCaptureFailure::UnsupportedFormat, "pipewire.invalid_plane_layout");
                return;
            }
            frame.planes[i] = {static_cast<int>(plane.fd), plane.mapoffset + chunk_offset,
                               plane.chunk->stride};
            if (buffer->n_datas == 1 &&
                (options.cpu_capture || format.modifier == DRM_FORMAT_MOD_LINEAR)) {
                const auto required =
                    static_cast<std::uint64_t>(frame.height - 1) * plane.chunk->stride +
                    static_cast<std::uint64_t>(frame.width) * 4;
                if ((options.cpu_capture && !plane.chunk->size) ||
                    (plane.chunk->size && plane.chunk->size < required)) {
                    fail(FrameCaptureFailure::UnsupportedFormat, "pipewire.short_chunk");
                    return;
                }
            }
            if (options.cpu_capture) {
                if (!plane.data || !plane.maxsize) {
                    fail(FrameCaptureFailure::UnsupportedFormat,
                         "pipewire.cpu_mapping_unavailable");
                    return;
                }
                frame.planes[i].offset = chunk_offset;
                frame.cpu_data = static_cast<const unsigned char *>(plane.data);
                frame.cpu_size = plane.maxsize;
                continue;
            }
            pollfd fence{static_cast<int>(plane.fd), POLLIN, 0};
            const auto wait = ::poll(&fence, 1, 1000);
            if (wait <= 0 || !(fence.revents & POLLIN) || (fence.revents & (POLLERR | POLLNVAL))) {
                metrics.native_error = wait < 0 ? errno : 0;
                fail(wait == 0 ? FrameCaptureFailure::Timeout : FrameCaptureFailure::GpuImport,
                     "pipewire.implicit_fence_wait");
                return;
            }
        }
        metrics.buffer.implicit_fences_ready = !options.cpu_capture;
        frame.diagnostics = metrics.buffer;
        const auto *header = static_cast<spa_meta_header *>(
            spa_buffer_find_meta_data(buffer, SPA_META_Header, sizeof(spa_meta_header)));
        if (header) {
            if (header->flags & SPA_META_HEADER_FLAG_CORRUPTED) {
                ++metrics.discarded;
                return;
            }
            if (last_sequence && !(header->flags & SPA_META_HEADER_FLAG_DISCONT) &&
                header->seq > *last_sequence)
                metrics.sequence_gaps += header->seq - *last_sequence - 1;
            last_sequence = header->seq;
            frame.timestamp_ns = header->pts;
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            const auto current = static_cast<std::int64_t>(now.tv_sec) * 1000000000LL + now.tv_nsec;
            presentation_timing.observe(metrics, header->pts, current,
                                        (header->flags & SPA_META_HEADER_FLAG_DISCONT) != 0);
        } else
            presentation_timing = {};
        if (options.cpu_capture) {
            ++metrics.cpu_frames;
            state = FrameCaptureState::Delivering;
            try {
                (*consumer)(frame);
                ++metrics.delivered;
                state = FrameCaptureState::Capturing;
            } catch (...) {
                fail(FrameCaptureFailure::Consumer);
            }
            return;
        }
        const auto import_start = std::chrono::steady_clock::now();
        const auto image = gpu->import(frame);
        const auto import_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - import_start)
                                   .count();
        if (image == EGL_NO_IMAGE_KHR) {
            metrics.native_error = eglGetError();
            ++metrics.discarded;
            fail(FrameCaptureFailure::GpuImport, "egl.import_dmabuf");
            return;
        }
        const FrameLease image_lease(gpu.get(), image, [](void *owner, void *image) noexcept {
            static_cast<GpuImporter *>(owner)->release(static_cast<EGLImageKHR>(image));
        });
        ++metrics.gpu_imports;
        metrics.gpu_import_time_sum_ms += import_ms;
        metrics.max_gpu_import_time_ms = std::max(metrics.max_gpu_import_time_ms, import_ms);
        frame.native_image = image;
        metrics.buffer.egl_image_imported = true;
        frame.diagnostics = metrics.buffer;
        state = FrameCaptureState::Delivering;
        try {
            (*consumer)(frame);
            ++metrics.delivered;
            state = FrameCaptureState::Capturing;
        } catch (...) {
            ++metrics.discarded;
            consumer_error = std::current_exception();
            fail(FrameCaptureFailure::Consumer);
        }
    }
};

PipeWireCapture::PipeWireCapture() : impl_(std::make_unique<Impl>()) {}
PipeWireCapture::~PipeWireCapture() {
    if (impl_->owner != std::this_thread::get_id() || impl_->state == FrameCaptureState::Delivering)
        std::terminate();
    impl_->cleanup();
}

void PipeWireCapture::start(const CaptureOptions &options) {
    auto &self = *impl_;
    self.check();
    if (self.state != FrameCaptureState::Idle && self.state != FrameCaptureState::Stopped)
        throw std::logic_error("Capture must be stopped before starting");
    if (options.width != 1920 || options.height != 1080 || options.fps != 60)
        throw std::invalid_argument("Initial capture target is 1920x1080 at 60 FPS");
    self.options = options;
    self.metrics = {};
    self.failure.reset();
    self.failure_stage = "pipewire.capture";
    self.last_sequence.reset();
    self.presentation_timing = {};
    self.consumer_error = {};
    self.state = FrameCaptureState::Starting;
    try {
        static std::once_flag initialized;
        std::call_once(initialized, [] { pw_init(nullptr, nullptr); });
        self.portal = std::make_unique<PortalSession>();
        int fd = self.portal->open();
        // Consume the fd only after the PipeWire context exists.
        self.loop = pw_main_loop_new(nullptr);
        if (self.loop)
            self.context = pw_context_new(pw_main_loop_get_loop(self.loop), nullptr, 0);
        if (!self.context) {
            ::close(fd);
            throw FrameCaptureError(FrameCaptureFailure::PipeWire);
        }
        self.core = pw_context_connect_fd(self.context, fd, nullptr, 0);
        if (!self.core)
            throw FrameCaptureError(FrameCaptureFailure::PipeWire);
        static const pw_core_events core_events = [] {
            pw_core_events events{};
            events.version = PW_VERSION_CORE_EVENTS;
            events.error = [](void *data, std::uint32_t, int, int result, const char *) {
                auto &capture = *static_cast<Impl *>(data);
                capture.metrics.native_error = result;
                capture.fail(FrameCaptureFailure::PipeWire);
            };
            return events;
        }();
        pw_core_add_listener(self.core, &self.core_listener, &core_events, &self);
        if (!options.cpu_capture)
            self.gpu = std::make_unique<GpuImporter>();
        auto *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                                             "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);
        if (!self.portal->serial().empty())
            pw_properties_set(properties, PW_KEY_TARGET_OBJECT, self.portal->serial().c_str());
        self.stream = pw_stream_new(self.core, "CloudPlay capture diagnostic", properties);
        if (!self.stream)
            throw FrameCaptureError(FrameCaptureFailure::PipeWire);
        static const pw_stream_events events = [] {
            pw_stream_events value{};
            value.version = PW_VERSION_STREAM_EVENTS;
            value.state_changed = [](void *data, pw_stream_state, pw_stream_state state,
                                     const char *) {
                auto &capture = *static_cast<Impl *>(data);
                if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)
                    capture.fail(FrameCaptureFailure::PipeWire);
            };
            value.param_changed = [](void *data, std::uint32_t id, const spa_pod *param) {
                if (id == SPA_PARAM_Format)
                    static_cast<Impl *>(data)->update_format(param);
            };
            value.process = [](void *data) {
                auto &capture = *static_cast<Impl *>(data);
                try {
                    capture.process();
                } catch (...) {
                    capture.fail(FrameCaptureFailure::GpuImport);
                }
            };
            return value;
        }();
        pw_stream_add_listener(self.stream, &self.listener, &events, &self);
        std::uint8_t storage[16384];
        auto builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        std::vector<const spa_pod *> params;
        const spa_rectangle size{options.width, options.height};
        const spa_fraction rate{options.fps, 1};
        const spa_fraction minimum_rate{0, 1};
        for (const auto &entry : formats) {
            const auto modifiers =
                options.cpu_capture ? std::vector<std::uint64_t>{} : self.gpu->modifiers(entry.drm);
            if (!options.cpu_capture && modifiers.empty())
                continue;
            spa_pod_frame object;
            spa_pod_frame choice;
            spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_Format,
                                        SPA_PARAM_EnumFormat);
            spa_pod_builder_add(&builder, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
                                SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                                SPA_FORMAT_VIDEO_format, SPA_POD_Id(entry.spa),
                                SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size),
                                SPA_FORMAT_VIDEO_framerate,
                                SPA_POD_CHOICE_RANGE_Fraction(&rate, &minimum_rate, &rate),
                                SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_Fraction(&rate), 0);
            if (!options.cpu_capture) {
                spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_modifier,
                                     SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
                spa_pod_builder_push_choice(&builder, &choice, SPA_CHOICE_Enum, 0);
                spa_pod_builder_long(&builder, static_cast<std::int64_t>(modifiers.front()));
                for (auto modifier : modifiers)
                    spa_pod_builder_long(&builder, static_cast<std::int64_t>(modifier));
                spa_pod_builder_pop(&builder, &choice);
            }
            params.push_back(static_cast<spa_pod *>(spa_pod_builder_pop(&builder, &object)));
        }
        if (params.empty())
            throw FrameCaptureError(FrameCaptureFailure::GpuImport);
        const auto flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_DONT_RECONNECT | PW_STREAM_FLAG_NO_CONVERT |
            (options.cpu_capture ? PW_STREAM_FLAG_MAP_BUFFERS : 0));
        if (pw_stream_connect(self.stream, PW_DIRECTION_INPUT,
                              self.portal->serial().empty() ? self.portal->node() : PW_ID_ANY,
                              flags, params.data(), static_cast<std::uint32_t>(params.size())) < 0)
            throw FrameCaptureError(FrameCaptureFailure::PipeWire);
        self.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    } catch (const FrameCaptureError &error) {
        self.fail(error.reason);
        self.cleanup();
        throw;
    } catch (...) {
        self.fail(FrameCaptureFailure::PipeWire);
        self.cleanup();
        throw;
    }
}

bool PipeWireCapture::poll(const Consumer &consume) {
    auto &self = *impl_;
    self.check();
    if (self.state != FrameCaptureState::Starting && self.state != FrameCaptureState::Capturing)
        throw std::logic_error("Capture is not running");
    const auto before = self.metrics.delivered;
    self.consumer = &consume;
    self.portal->pump();
    if (self.portal->closed())
        self.fail(FrameCaptureFailure::PermissionDenied);
    if (!self.failure && pw_loop_iterate(pw_main_loop_get_loop(self.loop), 0) < 0)
        self.fail(FrameCaptureFailure::PipeWire);
    self.consumer = nullptr;
    if (!self.failure && self.metrics.delivered == 0 &&
        std::chrono::steady_clock::now() > self.deadline)
        self.fail(FrameCaptureFailure::Timeout);
    if (self.failure) {
        const auto failure = *self.failure;
        self.cleanup();
        throw FrameCaptureError(failure, self.failure_stage, self.metrics.native_error);
    }
    return self.metrics.delivered > before;
}

void PipeWireCapture::stop() {
    auto &self = *impl_;
    self.check();
    if (self.state == FrameCaptureState::Delivering)
        throw std::logic_error("Cannot stop capture from consumer");
    self.state = FrameCaptureState::Stopping;
    self.cleanup();
    self.state = FrameCaptureState::Stopped;
}
FrameCaptureState PipeWireCapture::state() const {
    impl_->check();
    return impl_->state;
}
FrameCaptureMetrics PipeWireCapture::metrics() const {
    impl_->check();
    return impl_->metrics;
}
} // namespace cloudplay::capture
