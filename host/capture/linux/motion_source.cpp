#include "cloudplay/capture/source_presentation_timing.hpp"
#include "presentation-time-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string_view>

namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }

class MotionSource {
  public:
    explicit MotionSource(bool composition_control) : composition_control_(composition_control) {}
    MotionSource(const MotionSource &) = delete;
    MotionSource &operator=(const MotionSource &) = delete;
    ~MotionSource() {
        // All callbacks run on this thread. Destroy proxies before their borrowed owner.
        if (frame_)
            wl_callback_destroy(frame_);
        for (auto &slot : feedback_)
            if (slot.proxy)
                wp_presentation_feedback_destroy(slot.proxy);
        if (egl_display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (overlay_egl_surface_ != EGL_NO_SURFACE)
                eglDestroySurface(egl_display_, overlay_egl_surface_);
            if (egl_surface_ != EGL_NO_SURFACE)
                eglDestroySurface(egl_display_, egl_surface_);
            if (context_ != EGL_NO_CONTEXT)
                eglDestroyContext(egl_display_, context_);
            eglTerminate(egl_display_);
        }
        if (overlay_window_)
            wl_egl_window_destroy(overlay_window_);
        if (overlay_role_)
            wl_subsurface_destroy(overlay_role_);
        if (overlay_surface_)
            wl_surface_destroy(overlay_surface_);
        if (window_)
            wl_egl_window_destroy(window_);
        if (toplevel_)
            xdg_toplevel_destroy(toplevel_);
        if (xdg_surface_)
            xdg_surface_destroy(xdg_surface_);
        if (surface_)
            wl_surface_destroy(surface_);
        if (presentation_)
            wp_presentation_destroy(presentation_);
        if (shell_)
            xdg_wm_base_destroy(shell_);
        if (compositor_)
            wl_compositor_destroy(compositor_);
        if (subcompositor_)
            wl_subcompositor_destroy(subcompositor_);
        if (registry_)
            wl_registry_destroy(registry_);
        if (display_)
            wl_display_disconnect(display_);
    }

    int run(int seconds) {
        display_ = wl_display_connect(nullptr);
        require(display_ != nullptr, "Wayland connection unavailable");
        registry_ = wl_display_get_registry(display_);
        require(registry_ != nullptr, "Wayland registry unavailable");
        static constexpr wl_registry_listener registry_listener{global, global_removed};
        wl_registry_add_listener(registry_, &registry_listener, this);
        const auto startup_deadline = Clock::now() + std::chrono::seconds(5);
        while ((!compositor_ || !shell_ || !presentation_ ||
                (composition_control_ && !subcompositor_)) &&
               !interrupted) {
            require(Clock::now() < startup_deadline,
                    "Missing required compositor, shell, presentation or subsurface protocol "
                    "(startup timeout)");
            pump();
        }
        if (interrupted)
            return 130;
        surface_ = wl_compositor_create_surface(compositor_);
        require(surface_ != nullptr, "Surface allocation failed");
        xdg_surface_ = xdg_wm_base_get_xdg_surface(shell_, surface_);
        require(xdg_surface_ != nullptr, "XDG surface allocation failed");
        static constexpr xdg_surface_listener surface_listener{configure_surface};
        xdg_surface_add_listener(xdg_surface_, &surface_listener, this);
        toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
        require(toplevel_ != nullptr, "Toplevel allocation failed");
        static constexpr xdg_toplevel_listener toplevel_listener{configure_toplevel, close, nullptr,
                                                                 nullptr};
        xdg_toplevel_add_listener(toplevel_, &toplevel_listener, this);
        xdg_toplevel_set_title(toplevel_, "CloudPlay Presentation Reference");
        xdg_toplevel_set_app_id(toplevel_, "org.cloudplay.PresentationReference");
        xdg_toplevel_set_fullscreen(toplevel_, nullptr);
        wl_surface_commit(surface_);
        while (!configured_ && !interrupted && !closed_) {
            require(Clock::now() < startup_deadline, "Fullscreen configure timeout");
            pump();
        }
        if (interrupted || closed_)
            return 130;
        require(!failure_, failure_);
        initialize_egl();
        const auto start = Clock::now();
        const auto deadline = start + std::chrono::seconds(seconds);
        while (Clock::now() < deadline && !interrupted && !closed_) {
            require(!failure_, failure_);
            if (ready_)
                render();
            pump();
        }
        // Finish feedback already in flight without submitting more frames.
        draining_ = true;
        const auto drain_deadline = Clock::now() + std::chrono::seconds(1);
        while (pending() && Clock::now() < drain_deadline && !interrupted && !closed_)
            pump();
        require(!failure_, failure_);
        const auto stats = timing_.intervals.summary();
        std::cout << "{\"event\":\"source.summary\",\"width\":1920,\"height\":1080,"
                     "\"pacing\":\"wl_surface.frame\",\"clockId\":"
                  << clock_id_ << ",\"submitted\":" << submitted_
                  << ",\"presented\":" << timing_.count << ",\"discarded\":" << discarded_
                  << ",\"invalidFeedback\":" << timing_.invalid
                  << ",\"pendingFeedback\":" << pending()
                  << ",\"presentationSpanSeconds\":" << timing_.span_seconds()
                  << ",\"presentationFps\":" << timing_.fps() << ",\"refreshNs\":" << refresh_ns_
                  << ",\"flagsIntersection\":" << flags_ << ",\"intervalSamples\":" << stats.count
                  << ",\"compositionControl\":" << (composition_control_ ? "true" : "false")
                  << ",\"zeroCopyPresented\":" << zero_copy_presented_
                  << ",\"percentileOverflow\":" << stats.overflow
                  << ",\"intervalMs\":{\"p50\":" << stats.p50_ms << ",\"p95\":" << stats.p95_ms
                  << ",\"p99\":" << stats.p99_ms << ",\"max\":" << stats.max_ms
                  << "},\"completed\":" << (!interrupted && !closed_ ? "true" : "false") << "}\n";
        if (interrupted || closed_)
            return 130;
        return timing_.count > 1 && !timing_.invalid && !pending() ? 0 : 3;
    }

  private:
    struct Feedback {
        MotionSource *owner{};
        struct wp_presentation_feedback *proxy{};
    };
    static void require(bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message ? message : "Motion source failed");
    }
    std::size_t pending() const {
        std::size_t count{};
        for (const auto &slot : feedback_)
            count += slot.proxy != nullptr;
        return count;
    }
    static void global(void *data, wl_registry *registry, std::uint32_t name, const char *interface,
                       std::uint32_t) {
        auto &self = *static_cast<MotionSource *>(data);
        if (std::strcmp(interface, wl_compositor_interface.name) == 0 && !self.compositor_)
            self.compositor_ = static_cast<wl_compositor *>(
                wl_registry_bind(registry, name, &wl_compositor_interface, 1));
        else if (std::strcmp(interface, wl_subcompositor_interface.name) == 0 &&
                 self.composition_control_ && !self.subcompositor_)
            self.subcompositor_ = static_cast<wl_subcompositor *>(
                wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
        else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0 && !self.shell_) {
            self.shell_ = static_cast<xdg_wm_base *>(
                wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
            static constexpr xdg_wm_base_listener listener{ping};
            if (self.shell_)
                xdg_wm_base_add_listener(self.shell_, &listener, &self);
        } else if (std::strcmp(interface, wp_presentation_interface.name) == 0 &&
                   !self.presentation_) {
            self.presentation_ = static_cast<wp_presentation *>(
                wl_registry_bind(registry, name, &wp_presentation_interface, 1));
            static constexpr wp_presentation_listener listener{clock_id};
            if (self.presentation_)
                wp_presentation_add_listener(self.presentation_, &listener, &self);
        }
    }
    static void global_removed(void *, wl_registry *, std::uint32_t) {}
    static void ping(void *, xdg_wm_base *shell, std::uint32_t serial) {
        xdg_wm_base_pong(shell, serial);
    }
    static void clock_id(void *data, wp_presentation *, std::uint32_t id) {
        static_cast<MotionSource *>(data)->clock_id_ = id;
    }
    static void configure_toplevel(void *data, xdg_toplevel *, std::int32_t width,
                                   std::int32_t height, wl_array *states) {
        auto &self = *static_cast<MotionSource *>(data);
        bool fullscreen = false;
        const auto *values = static_cast<const std::uint32_t *>(states->data);
        for (std::size_t i = 0; i < states->size / sizeof(std::uint32_t); ++i)
            fullscreen |= values[i] == XDG_TOPLEVEL_STATE_FULLSCREEN;
        std::cerr << "Configured " << width << 'x' << height << ", fullscreen=" << fullscreen
                  << ", bufferScale=1\n";
        // Fixed geometry is an experiment constraint, not a request to rescale content.
        if (!fullscreen || width != 1920 || height != 1080)
            self.failure_ = "Expected fullscreen 1920x1080 at scale 1; check GNOME Displays";
    }
    static void configure_surface(void *data, xdg_surface *surface, std::uint32_t serial) {
        auto &self = *static_cast<MotionSource *>(data);
        xdg_surface_ack_configure(surface, serial);
        self.configured_ = true;
        if (!self.frame_)
            self.ready_ = true;
    }
    static void close(void *data, xdg_toplevel *) {
        static_cast<MotionSource *>(data)->closed_ = true;
    }
    static void frame_done(void *data, wl_callback *callback, std::uint32_t) {
        auto &self = *static_cast<MotionSource *>(data);
        wl_callback_destroy(callback);
        self.frame_ = nullptr;
        self.ready_ = true;
    }
    static void sync_output(void *, struct wp_presentation_feedback *, wl_output *) {}
    static void presented(void *data, struct wp_presentation_feedback *proxy, std::uint32_t hi,
                          std::uint32_t lo, std::uint32_t ns, std::uint32_t refresh, std::uint32_t,
                          std::uint32_t, std::uint32_t flags) {
        auto &slot = *static_cast<Feedback *>(data);
        auto &self = *slot.owner;
        self.timing_.observe(hi, lo, ns);
        self.refresh_ns_ = refresh;
        self.flags_ &= flags;
        if (flags & WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY)
            ++self.zero_copy_presented_;
        wp_presentation_feedback_destroy(proxy);
        slot.proxy = nullptr;
    }
    static void discarded(void *data, struct wp_presentation_feedback *proxy) {
        auto &slot = *static_cast<Feedback *>(data);
        ++slot.owner->discarded_;
        wp_presentation_feedback_destroy(proxy);
        slot.proxy = nullptr;
    }
    void pump() {
        // EGL can read the socket on its private queue, leaving our callbacks pending.
        while (wl_display_prepare_read(display_) != 0)
            require(wl_display_dispatch_pending(display_) >= 0, "Wayland dispatch failed");
        if (ready_ && !draining_) {
            wl_display_cancel_read(display_);
            return;
        }
        const auto flushed = wl_display_flush(display_);
        if (flushed < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display_);
            require(false, "Wayland flush failed");
        }
        pollfd fd{wl_display_get_fd(display_),
                  static_cast<short>(POLLIN | (flushed < 0 ? POLLOUT : 0)), 0};
        const int status = ::poll(&fd, 1, 20);
        if (status > 0 && (fd.revents & POLLIN))
            require(wl_display_read_events(display_) >= 0, "Wayland read failed");
        else
            wl_display_cancel_read(display_);
        require(status >= 0 || errno == EINTR, "Wayland poll failed");
        require(!(fd.revents & (POLLERR | POLLHUP | POLLNVAL)), "Wayland connection closed");
        require(wl_display_dispatch_pending(display_) >= 0, "Wayland dispatch failed");
    }
    void initialize_egl() {
        egl_display_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display_));
        require(egl_display_ != EGL_NO_DISPLAY && eglInitialize(egl_display_, nullptr, nullptr),
                "EGL Wayland initialization failed");
        require(eglBindAPI(EGL_OPENGL_ES_API), "EGL GLES binding failed");
        const EGLint attributes[]{EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT,
                                  EGL_RENDERABLE_TYPE,
                                  EGL_OPENGL_ES2_BIT,
                                  EGL_RED_SIZE,
                                  8,
                                  EGL_GREEN_SIZE,
                                  8,
                                  EGL_BLUE_SIZE,
                                  8,
                                  EGL_ALPHA_SIZE,
                                  0,
                                  EGL_NONE};
        EGLConfig config{};
        EGLint count{};
        require(eglChooseConfig(egl_display_, attributes, &config, 1, &count) && count == 1,
                "No EGL window configuration");
        const EGLint context_attributes[]{EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, context_attributes);
        require(context_ != EGL_NO_CONTEXT, "EGL context creation failed");
        window_ = wl_egl_window_create(surface_, 1920, 1080);
        require(window_ != nullptr, "Wayland EGL window creation failed");
        egl_surface_ = eglCreateWindowSurface(
            egl_display_, config, reinterpret_cast<EGLNativeWindowType>(window_), nullptr);
        require(egl_surface_ != EGL_NO_SURFACE &&
                    eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, context_),
                "EGL window binding failed");
        // Frame callbacks provide compositor pacing; do not count swaps as presentations.
        require(eglSwapInterval(egl_display_, 0), "EGL swap interval setup failed");
        if (composition_control_)
            initialize_overlay(config);
        const auto *renderer = glGetString(GL_RENDERER);
        std::cerr << "Initialized "
                  << (renderer ? reinterpret_cast<const char *>(renderer) : "unknown")
                  << " renderer.\n";
    }
    void initialize_overlay(EGLConfig config) {
        overlay_surface_ = wl_compositor_create_surface(compositor_);
        require(overlay_surface_ != nullptr, "Overlay surface allocation failed");
        overlay_role_ = wl_subcompositor_get_subsurface(subcompositor_, overlay_surface_, surface_);
        require(overlay_role_ != nullptr, "Overlay subsurface allocation failed");
        wl_subsurface_set_position(overlay_role_, 1856, 16);
        overlay_window_ = wl_egl_window_create(overlay_surface_, 48, 48);
        require(overlay_window_ != nullptr, "Overlay EGL window creation failed");
        overlay_egl_surface_ = eglCreateWindowSurface(
            egl_display_, config, reinterpret_cast<EGLNativeWindowType>(overlay_window_), nullptr);
        require(
            overlay_egl_surface_ != EGL_NO_SURFACE &&
                eglMakeCurrent(egl_display_, overlay_egl_surface_, overlay_egl_surface_, context_),
            "Overlay EGL binding failed");
        require(eglSwapInterval(egl_display_, 0), "Overlay swap interval setup failed");
        glClearColor(0.25F, 0.25F, 0.25F, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        require(glGetError() == GL_NO_ERROR, "Overlay rendering failed");
        // Synchronized child content becomes visible on the first parent swap.
        require(eglSwapBuffers(egl_display_, overlay_egl_surface_), "Overlay swap failed");
        require(eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, context_),
                "Parent EGL binding failed");
    }
    void render() {
        Feedback *available{};
        for (auto &slot : feedback_)
            if (!slot.proxy) {
                available = &slot;
                break;
            }
        require(available != nullptr, "Presentation feedback stalled (64 pending submissions)");
        const auto rectangle = [](int x, int y, int w, int h, float r, float g, float b) {
            glScissor(x, y, w, h);
            glClearColor(r, g, b, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        };
        glEnable(GL_SCISSOR_TEST);
        constexpr std::array<std::array<float, 3>, 6> colors{
            {{1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}}};
        for (int i = 0; i < 6; ++i) {
            const auto &c = colors[static_cast<std::size_t>(i)];
            rectangle(i * 320, 0, 320, 1080, c[0], c[1], c[2]);
        }
        rectangle(static_cast<int>((submitted_ * 8) % 1888), 100, 32, 880, 1, 1, 1);
        // A binary submission ID makes consecutive source frames distinguishable.
        for (int bit = 0; bit < 32; ++bit) {
            const float level = (submitted_ >> bit) & 1 ? 1.0F : 0.0F;
            rectangle(bit * 60, 0, 60, 60, level, level, level);
        }
        require(glGetError() == GL_NO_ERROR, "GLES pattern rendering failed");
        available->owner = this;
        available->proxy = wp_presentation_feedback(presentation_, surface_);
        require(available->proxy != nullptr, "Presentation feedback allocation failed");
        static constexpr wp_presentation_feedback_listener feedback_listener{sync_output, presented,
                                                                             discarded};
        wp_presentation_feedback_add_listener(available->proxy, &feedback_listener, available);
        frame_ = wl_surface_frame(surface_);
        require(frame_ != nullptr, "Frame callback allocation failed");
        static constexpr wl_callback_listener callback_listener{frame_done};
        wl_callback_add_listener(frame_, &callback_listener, this);
        ready_ = false;
        require(eglSwapBuffers(egl_display_, egl_surface_), "EGL swap failed");
        ++submitted_;
    }

    wl_display *display_{};
    wl_registry *registry_{};
    wl_compositor *compositor_{};
    wl_subcompositor *subcompositor_{};
    wl_surface *overlay_surface_{};
    wl_subsurface *overlay_role_{};
    wl_egl_window *overlay_window_{};
    EGLSurface overlay_egl_surface_{EGL_NO_SURFACE};
    xdg_wm_base *shell_{};
    wl_surface *surface_{};
    xdg_surface *xdg_surface_{};
    xdg_toplevel *toplevel_{};
    wp_presentation *presentation_{};
    wl_callback *frame_{};
    wl_egl_window *window_{};
    EGLDisplay egl_display_{EGL_NO_DISPLAY};
    EGLContext context_{EGL_NO_CONTEXT};
    EGLSurface egl_surface_{EGL_NO_SURFACE};
    std::array<Feedback, 64> feedback_{};
    cloudplay::capture::SourcePresentationTiming timing_;
    std::uint64_t submitted_{}, discarded_{};
    std::uint64_t zero_copy_presented_{};
    const bool composition_control_;
    std::uint32_t clock_id_{UINT32_MAX}, refresh_ns_{}, flags_{UINT32_MAX};
    const char *failure_{};
    bool configured_{}, ready_{}, closed_{}, draining_{};
};
} // namespace

int main(int argc, char **argv) {
    int seconds = 90;
    bool composition_control = false;
    bool duration_set = false;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "cloudplay_wayland_motion [--seconds 1..300] [--composition-control]\n";
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        bool valid = false;
        if (argument == "--composition-control" && !composition_control) {
            composition_control = true;
            valid = true;
        } else if (argument == "--seconds" && !duration_set && i + 1 < argc) {
            duration_set = true;
            const std::string_view value(argv[++i]);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
            valid = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                    seconds >= 1 && seconds <= 300;
        }
        if (!valid) {
            std::cerr << "Expected --seconds 1..300 and optional --composition-control, without "
                         "duplicates\n";
            return 2;
        }
    }
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);
    try {
        MotionSource source(composition_control);
        return source.run(seconds);
    } catch (const std::exception &error) {
        std::cerr << "Motion source: " << error.what() << '\n';
        return 1;
    }
}
