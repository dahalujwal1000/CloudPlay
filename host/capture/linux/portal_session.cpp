#include "portal_session.hpp"
#include <algorithm>
#include <chrono>
#include <cloudplay/capture/frame_capture.hpp>
#include <cstdlib>
#include <thread>

namespace cloudplay::capture {
namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto screen_cast = "org.freedesktop.portal.ScreenCast";
constexpr auto desktop = "/org/freedesktop/portal/desktop";

struct Response {
    bool done{};
    guint code{2};
    GVariant *results{};
    ~Response() {
        if (results)
            g_variant_unref(results);
    }
};

class ContextScope final {
  public:
    explicit ContextScope(GMainContext *context) : context_(context) {
        g_main_context_push_thread_default(context_);
    }
    ~ContextScope() { g_main_context_pop_thread_default(context_); }

  private:
    GMainContext *context_;
};

GVariant *options(const std::string &token, bool create = false, bool select = false) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    if (create)
        g_variant_builder_add(&builder, "{sv}", "session_handle_token",
                              g_variant_new_string(token.c_str()));
    if (select) {
        g_variant_builder_add(&builder, "{sv}", "types", g_variant_new_uint32(3));
        g_variant_builder_add(&builder, "{sv}", "multiple", g_variant_new_boolean(false));
        // Hidden cursor is the protocol default; no persistent permission/token requested.
    }
    return g_variant_builder_end(&builder);
}
} // namespace

PortalSession::PortalSession() : context_(g_main_context_new()) {}
PortalSession::~PortalSession() {
    close();
    g_main_context_unref(context_);
}

std::string PortalSession::token() { return "cloudplay" + std::to_string(++counter_); }

void PortalSession::pump() {
    for (unsigned i = 0; i < 16 && g_main_context_pending(context_); ++i)
        g_main_context_iteration(context_, false);
}

GVariant *PortalSession::request(const char *method, GVariant *parameters,
                                 const std::string &token) {
    Response response;
    const auto path = "/org/freedesktop/portal/desktop/request/" + sender_ + "/" + token;
    const auto subscription = g_dbus_connection_signal_subscribe(
        bus_, service, "org.freedesktop.portal.Request", "Response", path.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
           GVariant *value, gpointer data) {
            auto &reply = *static_cast<Response *>(data);
            if (!reply.done) {
                g_variant_get(value, "(u@a{sv})", &reply.code, &reply.results);
                reply.done = true;
            }
        },
        &response, nullptr);
    GError *error{};
    auto *reply = g_dbus_connection_call_sync(bus_, service, desktop, screen_cast, method,
                                              parameters, G_VARIANT_TYPE("(o)"),
                                              G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    bool valid_handle = false;
    if (reply) {
        const char *handle{};
        g_variant_get(reply, "(&o)", &handle);
        valid_handle = path == handle;
        g_variant_unref(reply);
    }
    if (error)
        g_error_free(error);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (valid_handle && !response.done && std::chrono::steady_clock::now() < deadline) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Unsubscribe while the response storage is still alive; callbacks only run on our context.
    g_dbus_connection_signal_unsubscribe(bus_, subscription);
    if (!valid_handle || !response.done) {
        auto *closed = g_dbus_connection_call_sync(
            bus_, service, path.c_str(), "org.freedesktop.portal.Request", "Close", nullptr,
            nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        if (closed)
            g_variant_unref(closed);
        throw FrameCaptureError(valid_handle ? FrameCaptureFailure::Timeout
                                             : FrameCaptureFailure::Portal);
    }
    if (response.code != 0)
        throw FrameCaptureError(FrameCaptureFailure::PermissionDenied);
    return g_variant_ref(response.results);
}

int PortalSession::open() {
    const auto *type = std::getenv("XDG_SESSION_TYPE");
    if (!type || std::string_view(type) != "wayland" || !std::getenv("DBUS_SESSION_BUS_ADDRESS"))
        throw FrameCaptureError(FrameCaptureFailure::SessionUnavailable);
    const ContextScope scope(context_);
    GError *error{};
    bus_ = g_dbus_connection_new_for_address_sync(
        std::getenv("DBUS_SESSION_BUS_ADDRESS"),
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    if (!bus_) {
        if (error)
            g_error_free(error);
        throw FrameCaptureError(FrameCaptureFailure::SessionUnavailable);
    }
    sender_ = g_dbus_connection_get_unique_name(bus_);
    sender_.erase(0, 1);
    std::replace(sender_.begin(), sender_.end(), '.', '_');
    auto handle = token();
    auto *created =
        request("CreateSession", g_variant_new("(@a{sv})", options(handle, true)), handle);
    const char *session{};
    if (!g_variant_lookup(created, "session_handle", "&s", &session) ||
        !g_variant_is_object_path(session)) {
        g_variant_unref(created);
        throw FrameCaptureError(FrameCaptureFailure::Portal);
    }
    session_ = session;
    g_variant_unref(created);
    closed_subscription_ = g_dbus_connection_signal_subscribe(
        bus_, service, "org.freedesktop.portal.Session", "Closed", session_.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
           GVariant *, gpointer data) { static_cast<PortalSession *>(data)->closed_ = true; },
        this, nullptr);
    handle = token();
    auto *selected =
        request("SelectSources",
                g_variant_new("(o@a{sv})", session_.c_str(), options(handle, false, true)), handle);
    g_variant_unref(selected);
    handle = token();
    auto *started = request(
        "Start", g_variant_new("(os@a{sv})", session_.c_str(), "", options(handle)), handle);
    auto *streams = g_variant_lookup_value(started, "streams", G_VARIANT_TYPE("a(ua{sv})"));
    g_variant_unref(started);
    if (!streams || g_variant_n_children(streams) != 1) {
        if (streams)
            g_variant_unref(streams);
        throw FrameCaptureError(FrameCaptureFailure::Portal);
    }
    GVariant *properties{};
    g_variant_get_child(streams, 0, "(u@a{sv})", &node_, &properties);
    guint64 serial{};
    if (g_variant_lookup(properties, "pipewire-serial", "t", &serial))
        serial_ = std::to_string(serial);
    g_variant_unref(properties);
    g_variant_unref(streams);
    GUnixFDList *fds{};
    auto *remote = g_dbus_connection_call_with_unix_fd_list_sync(
        bus_, service, desktop, screen_cast, "OpenPipeWireRemote",
        g_variant_new("(o@a{sv})", session_.c_str(), options(token())), G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &fds, nullptr, &error);
    int fd = -1;
    if (remote && fds) {
        gint index{};
        g_variant_get(remote, "(h)", &index);
        fd = g_unix_fd_list_get(fds, index, &error);
    }
    if (remote)
        g_variant_unref(remote);
    if (fds)
        g_object_unref(fds);
    if (error)
        g_error_free(error);
    if (fd < 0)
        throw FrameCaptureError(FrameCaptureFailure::Portal);
    return fd;
}

void PortalSession::close() noexcept {
    if (!bus_)
        return;
    if (closed_subscription_)
        g_dbus_connection_signal_unsubscribe(bus_, closed_subscription_);
    closed_subscription_ = 0;
    if (!session_.empty()) {
        auto *result = g_dbus_connection_call_sync(
            bus_, service, session_.c_str(), "org.freedesktop.portal.Session", "Close", nullptr,
            nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        if (result)
            g_variant_unref(result);
    }
    g_dbus_connection_close_sync(bus_, nullptr, nullptr);
    g_object_unref(bus_);
    bus_ = nullptr;
    session_.clear();
    closed_ = false;
}
} // namespace cloudplay::capture
