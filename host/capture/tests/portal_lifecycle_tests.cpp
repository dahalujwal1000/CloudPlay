#include <algorithm>
#include <atomic>
#include <cloudplay/capture/pipewire_capture.hpp>
#include <future>
#include <gio/gio.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr auto xml = R"(<node>
<interface name='org.freedesktop.portal.ScreenCast'>
<method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<method name='SelectSources'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<method name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<property name='AvailableCursorModes' type='u' access='read'/>
</interface><interface name='org.freedesktop.portal.Session'><method name='Close'/></interface>
</node>)";
constexpr auto session_path = "/org/freedesktop/portal/desktop/session/test/session";

class TestPortal final {
  public:
    TestPortal() {
        std::promise<void> ready;
        auto future = ready.get_future();
        worker_ = std::thread([this, &ready] {
            auto *context = g_main_context_new();
            g_main_context_push_thread_default(context);
            loop_ = g_main_loop_new(context, false);
            // GDBusConnectionFlags is a bitmask, including these two combined flags.
            // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
            const auto flags =
                static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
            // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
            auto *bus = g_dbus_connection_new_for_address_sync(g_getenv("DBUS_SESSION_BUS_ADDRESS"),
                                                               flags, nullptr, nullptr, nullptr);
            auto *info = g_dbus_node_info_new_for_xml(xml, nullptr);
            const GDBusInterfaceVTable callbacks{method, property, nullptr, {nullptr}};
            const auto screen_id = g_dbus_connection_register_object(
                bus, "/org/freedesktop/portal/desktop", info->interfaces[0], &callbacks, this,
                nullptr, nullptr);
            const auto session_id = g_dbus_connection_register_object(
                bus, session_path, info->interfaces[1], &callbacks, this, nullptr, nullptr);
            auto *reply = g_dbus_connection_call_sync(
                bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                "RequestName", g_variant_new("(su)", "org.freedesktop.portal.Desktop", 0U),
                G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
            if (reply)
                g_variant_unref(reply);
            ready.set_value();
            g_main_loop_run(loop_);
            g_dbus_connection_unregister_object(bus, screen_id);
            g_dbus_connection_unregister_object(bus, session_id);
            g_dbus_node_info_unref(info);
            g_dbus_connection_close_sync(bus, nullptr, nullptr);
            g_object_unref(bus);
            g_main_loop_unref(loop_);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context);
        });
        future.get();
    }
    ~TestPortal() {
        g_main_loop_quit(loop_);
        worker_.join();
    }
    std::atomic<unsigned> closes{};
    std::atomic<unsigned> denied_stage{2};
    std::atomic<unsigned> cursor_modes{3}, embedded_requests{};

  private:
    static GVariant *property(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                              const gchar *, GError **, gpointer data) {
        return g_variant_new_uint32(static_cast<TestPortal *>(data)->cursor_modes);
    }
    static void method(GDBusConnection *bus, const gchar *sender, const gchar *, const gchar *,
                       const gchar *name, GVariant *parameters, GDBusMethodInvocation *invocation,
                       gpointer data) {
        auto &server = *static_cast<TestPortal *>(data);
        if (std::string_view(name) == "Close") {
            ++server.closes;
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        auto *options = g_variant_get_child_value(parameters, g_variant_n_children(parameters) - 1);
        const char *token{};
        g_variant_lookup(options, "handle_token", "&s", &token);
        if (std::string_view(name) == "SelectSources") {
            guint mode{};
            if (g_variant_lookup(options, "cursor_mode", "u", &mode) && mode == 2)
                ++server.embedded_requests;
        }
        std::string peer(sender + 1);
        std::replace(peer.begin(), peer.end(), '.', '_');
        const auto path = "/org/freedesktop/portal/desktop/request/" + peer + "/" + token;
        g_variant_unref(options);
        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
        if (std::string_view(name) == "CreateSession")
            g_variant_builder_add(&results, "{sv}", "session_handle",
                                  g_variant_new_string(session_path));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", path.c_str()));
        const auto stage = std::string_view(name) == "CreateSession"   ? 0U
                           : std::string_view(name) == "SelectSources" ? 1U
                                                                       : 2U;
        const guint code = stage == server.denied_stage ? 1 : 0;
        g_dbus_connection_emit_signal(
            bus, sender, path.c_str(), "org.freedesktop.portal.Request", "Response",
            g_variant_new("(u@a{sv})", code, g_variant_builder_end(&results)), nullptr);
    }
    GMainLoop *loop_{};
    std::thread worker_;
};
} // namespace

int main() {
    auto *test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(test_bus);
    g_setenv("XDG_SESSION_TYPE", "wayland", true);
    {
        TestPortal server;
        constexpr std::string_view stages[]{"CreateSession", "SelectSources", "Start"};
        unsigned expected_closes{};
        for (unsigned stage = 0; stage < 3; ++stage) {
            server.denied_stage = stage;
            for (unsigned i = 0; i < 3; ++i) {
                {
                    cloudplay::capture::PipeWireCapture capture;
                    try {
                        cloudplay::capture::CaptureOptions options;
                        options.embedded_cursor = i == 2;
                        capture.start(options);
                        throw std::runtime_error("Cancelled portal accepted");
                    } catch (const cloudplay::capture::FrameCaptureError &error) {
                        if (error.reason !=
                                cloudplay::capture::FrameCaptureFailure::PermissionDenied ||
                            error.operation != stages[stage] || error.native_code != 1 ||
                            capture.state() != cloudplay::capture::FrameCaptureState::Failed)
                            throw std::runtime_error("Wrong portal cancellation diagnostic");
                    }
                    if (i != 0) {
                        capture.stop();
                        capture.stop();
                        if (capture.state() != cloudplay::capture::FrameCaptureState::Stopped)
                            throw std::runtime_error("Repeated stop failed");
                    }
                }
                // CreateSession denial never grants a session to close.
                expected_closes += stage == 0 ? 0 : 1;
                if (server.closes != expected_closes)
                    throw std::runtime_error("Portal session leaked or closed twice");
            }
        }
        if (server.embedded_requests != 2)
            throw std::runtime_error("Embedded cursor not requested at SelectSources");
        server.cursor_modes = 1;
        cloudplay::capture::PipeWireCapture capture;
        cloudplay::capture::CaptureOptions options;
        options.embedded_cursor = true;
        bool unsupported{};
        try {
            capture.start(options);
        } catch (const cloudplay::capture::FrameCaptureError &error) {
            unsupported =
                error.reason == cloudplay::capture::FrameCaptureFailure::UnsupportedFormat &&
                error.operation == "portal.embedded_cursor_unavailable";
        }
        capture.stop();
        if (!unsupported || server.closes != expected_closes)
            throw std::runtime_error("Unsupported cursor mode not rejected cleanly");
    }
    g_test_dbus_down(test_bus);
    g_object_unref(test_bus);
}
