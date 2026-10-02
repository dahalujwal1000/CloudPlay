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
            auto *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
            auto *info = g_dbus_node_info_new_for_xml(xml, nullptr);
            const GDBusInterfaceVTable callbacks{method, nullptr, nullptr, {nullptr}};
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

  private:
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
        const guint code = std::string_view(name) == "Start" ? 1 : 0;
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
        for (unsigned i = 0; i < 3; ++i) {
            cloudplay::capture::PipeWireCapture capture;
            try {
                capture.start({});
                throw std::runtime_error("Cancelled portal accepted");
            } catch (const cloudplay::capture::FrameCaptureError &error) {
                if (error.reason != cloudplay::capture::FrameCaptureFailure::PermissionDenied ||
                    error.operation != "Start")
                    throw std::runtime_error("Wrong portal cancellation diagnostic");
            }
            capture.stop();
            capture.stop();
            if (server.closes != i + 1 ||
                capture.state() != cloudplay::capture::FrameCaptureState::Stopped)
                throw std::runtime_error("Portal session leaked or closed twice");
        }
    }
    g_test_dbus_down(test_bus);
    g_object_unref(test_bus);
}
