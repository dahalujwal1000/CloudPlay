#include "../linux/key_mapping.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cloudplay/app/input_session.hpp>
#include <cloudplay/input/portal_input.hpp>
#include <future>
#include <gio/gio.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace cloudplay::input;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Portal input test failed");
}
constexpr auto session_path = "/org/freedesktop/portal/desktop/session/test/input";
constexpr auto xml = R"(<node>
<interface name='org.freedesktop.portal.RemoteDesktop'>
<method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<method name='SelectDevices'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<method name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<method name='NotifyKeyboardKeycode'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/><arg type='u' direction='in'/></method>
<method name='NotifyPointerButton'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/><arg type='u' direction='in'/></method>
<method name='NotifyPointerMotion'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='d' direction='in'/><arg type='d' direction='in'/></method>
<property name='AvailableDeviceTypes' type='u' access='read'/>
</interface><interface name='org.freedesktop.portal.Session'><method name='Close'/></interface>
</node>)";
class TestPortal final {
  public:
    TestPortal() {
        std::promise<void> ready;
        auto future = ready.get_future();
        worker_ = std::thread([this, &ready] {
            auto *context = g_main_context_new();
            g_main_context_push_thread_default(context);
            loop_ = g_main_loop_new(context, false);
            const auto flags =
                static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
            auto *bus = g_dbus_connection_new_for_address_sync(g_getenv("DBUS_SESSION_BUS_ADDRESS"),
                                                               flags, nullptr, nullptr, nullptr);
            auto *info = g_dbus_node_info_new_for_xml(xml, nullptr);
            const GDBusInterfaceVTable callbacks{method, property, nullptr, {nullptr}};
            const auto remote_id = g_dbus_connection_register_object(
                bus, "/org/freedesktop/portal/desktop", info->interfaces[0], &callbacks, this,
                nullptr, nullptr);
            const auto session_id = g_dbus_connection_register_object(
                bus, session_path, info->interfaces[1], &callbacks, this, nullptr, nullptr);
            auto *reply = g_dbus_connection_call_sync(
                bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                "RequestName", g_variant_new("(su)", "org.freedesktop.portal.Desktop", 0U),
                G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
            check(reply && remote_id && session_id);
            g_variant_unref(reply);
            ready.set_value();
            g_main_loop_run(loop_);
            g_dbus_connection_unregister_object(bus, remote_id);
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
    std::atomic<unsigned> denied_stage{3}, available{3}, granted{3}, closes{}, notifications{};
    std::atomic<int> last_code{};
    std::atomic<unsigned> last_state{};
    std::atomic<bool> fail_next{}, revoke_next{}, withhold_start{}, valid_options{true};
    std::atomic<double> last_dx{}, last_dy{};

  private:
    GMainLoop *loop_{};
    std::thread worker_;
    static GVariant *property(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                              const gchar *, GError **, gpointer data) {
        return g_variant_new_uint32(static_cast<TestPortal *>(data)->available);
    }
    static void method(GDBusConnection *bus, const gchar *sender, const gchar *, const gchar *,
                       const gchar *name, GVariant *parameters, GDBusMethodInvocation *invocation,
                       gpointer data) {
        auto &s = *static_cast<TestPortal *>(data);
        const std::string_view method(name);
        if (method == "Close") {
            ++s.closes;
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        if (method.starts_with("Notify")) {
            ++s.notifications;
            const char *path{};
            GVariant *opts{};
            if (method == "NotifyPointerMotion") {
                double dx{}, dy{};
                g_variant_get(parameters, "(&o@a{sv}dd)", &path, &opts, &dx, &dy);
                s.last_dx = dx;
                s.last_dy = dy;
            } else {
                gint code{};
                guint state{};
                g_variant_get(parameters, "(&o@a{sv}iu)", &path, &opts, &code, &state);
                s.last_code = code;
                s.last_state = state;
            }
            if (std::string_view(path) != session_path || g_variant_n_children(opts) != 0)
                s.valid_options = false;
            g_variant_unref(opts);
            if (s.revoke_next.exchange(false))
                g_dbus_connection_emit_signal(bus, sender, session_path,
                                              "org.freedesktop.portal.Session", "Closed",
                                              g_variant_new("(a{sv})", nullptr), nullptr);
            if (s.fail_next.exchange(false))
                g_dbus_method_invocation_return_dbus_error(
                    invocation, "org.freedesktop.portal.Error.Failed", "Test failure");
            else
                g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        const auto stage = method == "CreateSession" ? 0U : method == "SelectDevices" ? 1U : 2U;
        auto *opts = g_variant_get_child_value(parameters, g_variant_n_children(parameters) - 1);
        const char *token{};
        check(g_variant_lookup(opts, "handle_token", "&s", &token));
        if (stage == 1) {
            guint devices{};
            if (!g_variant_lookup(opts, "types", "u", &devices) || devices != 3)
                s.valid_options = false;
        }
        for (const auto *key : {"persist_mode", "restore_token", "clipboard_enabled"}) {
            auto *value = g_variant_lookup_value(opts, key, nullptr);
            if (value) {
                s.valid_options = false;
                g_variant_unref(value);
            }
        }
        std::string peer(sender + 1);
        std::replace(peer.begin(), peer.end(), '.', '_');
        const auto path = "/org/freedesktop/portal/desktop/request/" + peer + "/" + token;
        g_variant_unref(opts);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", path.c_str()));
        if (stage == 2 && s.withhold_start)
            return;
        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
        if (stage == 0)
            g_variant_builder_add(&results, "{sv}", "session_handle",
                                  g_variant_new_string(session_path));
        if (stage == 2)
            g_variant_builder_add(&results, "{sv}", "devices", g_variant_new_uint32(s.granted));
        g_dbus_connection_emit_signal(bus, sender, path.c_str(), "org.freedesktop.portal.Request",
                                      "Response",
                                      g_variant_new("(u@a{sv})", stage == s.denied_stage ? 1U : 0U,
                                                    g_variant_builder_end(&results)),
                                      nullptr);
    }
};
void expect_failure(PortalInputFailure reason, unsigned timeout = 500) {
    PortalInputSink sink;
    try {
        sink.start(timeout);
        throw std::runtime_error("Portal operation unexpectedly succeeded");
    } catch (const PortalInputError &error) {
        check(error.reason == reason && sink.state() == PortalInputState::Failed);
    }
    check(!sink.apply(MouseMotion{1, 1}));
    sink.close();
    sink.close();
}
} // namespace

int main() {
    for (std::uint16_t usage = 0; usage < 256; ++usage) {
        const bool accepted = (usage >= 0x04 && usage <= 0x65) ||
                              (usage >= 0x67 && usage <= 0x73) || (usage >= 0xe0 && usage <= 0xe7);
        check(evdev_key(usage).has_value() == accepted);
    }
    check(evdev_key(0x1a) == KEY_W && evdev_key(0xe0) == KEY_LEFTCTRL && !evdev_key(0xffff));
    check(evdev_button(1) == BTN_LEFT && evdev_button(5) == BTN_EXTRA && !evdev_button(0) &&
          !evdev_button(6));
    auto *test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(test_bus);
    g_setenv("XDG_SESSION_TYPE", "wayland", true);
    {
        TestPortal server;
        for (unsigned stage = 0; stage < 3; ++stage) {
            server.denied_stage = stage;
            expect_failure(PortalInputFailure::Denied);
        }
        check(server.closes == 2);
        server.denied_stage = 3;
        server.available = 1;
        expect_failure(PortalInputFailure::Unsupported);
        check(server.closes == 2);
        server.available = 3;
        server.granted = 1;
        expect_failure(PortalInputFailure::Denied);
        check(server.closes == 3);
        server.granted = 3;
        server.withhold_start = true;
        expect_failure(PortalInputFailure::Timeout, 10);
        check(server.closes == 4);
        server.withhold_start = false;
        {
            PortalInputSink sink;
            check(!sink.apply(KeyAction{0x1a, true}));
            sink.start(500);
            check(sink.state() == PortalInputState::Active &&
                  sink.diagnostics().granted_devices == 3);
            check(sink.apply(KeyAction{0x1a, true}) && server.last_code == KEY_W &&
                  server.last_state == 1);
            check(sink.apply(KeyAction{0x1a, false}) && server.last_state == 0);
            check(sink.apply(MouseMotion{10, -20}) && server.last_dx == 10 &&
                  server.last_dy == -20);
            check(sink.apply(MouseButtonAction{1, true}) && server.last_code == BTN_LEFT);
            check(sink.apply(MouseButtonAction{1, false}) && server.last_state == 0);
            const auto before = server.notifications.load();
            check(!sink.apply(KeyAction{0x66, true}) && !sink.apply(MouseMotion{4097, 0}) &&
                  !sink.apply(MouseButtonAction{6, true}));
            check(server.notifications == before && sink.diagnostics().invalid_payloads == 3);
            // Two HID usages alias one evdev key; releasing one must not release both.
            check(sink.apply(KeyAction{0x31, true}));
            check(sink.apply(KeyAction{0x32, true}));
            check(sink.apply(KeyAction{0x31, false}));
            check(server.notifications == before + 1);
            check(sink.apply(KeyAction{0x32, false}) && server.notifications == before + 2);
            server.fail_next = true;
            check(!sink.apply(KeyAction{0x1a, true}) &&
                  sink.last_failure() == PortalInputFailure::Call);
            check(sink.apply(KeyAction{0x1a, false}));
            server.revoke_next = true;
            check(sink.apply(MouseMotion{1, 1}));
            for (unsigned i = 0; i < 100 && sink.state() == PortalInputState::Active; ++i) {
                sink.pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(sink.state() == PortalInputState::Revoked && !sink.apply(MouseMotion{1, 1}));
            sink.close();
            sink.close();
            check(sink.state() == PortalInputState::Closed);
            try {
                sink.start();
                return 1;
            } catch (const std::logic_error &) {
            }
        }
        check(server.closes == 5 && server.valid_options);
        {
            auto sink = std::make_unique<PortalInputSink>();
            sink->start(500);
            cloudplay::app::InputSession input(1, std::move(sink));
            std::array<std::uint8_t, 34> packet{};
            packet[0] = 'C';
            packet[1] = 'P';
            packet[2] = 'I';
            packet[3] = 'N';
            packet[4] = packet[5] = packet[15] = packet[23] = packet[31] = 1;
            packet[7] = 2;
            packet[33] = 0x1a;
            check(std::get<EnqueueStatus>(input.receive(packet)) == EnqueueStatus::Accepted);
            check(input.drain() == 1 && server.last_state == 1);
            server.fail_next = true;
            check(!input.close() && input.state() == DispatchState::CleanupFailed);
            check(input.close() && input.state() == DispatchState::Closed &&
                  server.last_state == 0);
        }
        check(server.closes == 6);
    }
    g_test_dbus_down(test_bus);
    g_object_unref(test_bus);
    g_unsetenv("DBUS_SESSION_BUS_ADDRESS");
    expect_failure(PortalInputFailure::SessionUnavailable);
}
