#include "key_mapping.hpp"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cloudplay/input/portal_input.hpp>
#include <cstdlib>
#include <gio/gio.h>
#include <string>
#include <thread>

namespace cloudplay::input {
namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto desktop = "/org/freedesktop/portal/desktop";
constexpr auto remote = "org.freedesktop.portal.RemoteDesktop";
struct Response {
    bool done{};
    guint code{2};
    GVariant *results{};
    ~Response() {
        if (results)
            g_variant_unref(results);
    }
};
struct ContextScope {
    GMainContext *context;
    explicit ContextScope(GMainContext *value) : context(value) {
        g_main_context_push_thread_default(context);
    }
    ~ContextScope() { g_main_context_pop_thread_default(context); }
};
GVariant *options(const std::string &token, bool create = false, bool select = false) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    if (create)
        g_variant_builder_add(&builder, "{sv}", "session_handle_token",
                              g_variant_new_string(token.c_str()));
    if (select)
        g_variant_builder_add(&builder, "{sv}", "types", g_variant_new_uint32(3));
    return g_variant_builder_end(&builder);
}
GVariant *empty_options() { return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0); }
} // namespace

struct PortalInputSink::Impl {
    GMainContext *context{g_main_context_new()};
    GDBusConnection *bus{};
    std::string session, sender;
    guint subscription{};
    unsigned counter{};
    PortalInputState state{PortalInputState::Idle};
    PortalInputFailure failure{PortalInputFailure::None};
    PortalInputDiagnostics metrics;
    std::bitset<256> held;
    std::bitset<5> buttons;
    ~Impl() {
        close();
        g_main_context_unref(context);
    }
    void pump() noexcept {
        for (unsigned i = 0; i < 16 && g_main_context_pending(context); ++i)
            g_main_context_iteration(context, false);
        if (bus && g_dbus_connection_is_closed(bus) && state == PortalInputState::Active) {
            state = PortalInputState::Revoked;
            failure = PortalInputFailure::SessionUnavailable;
        }
    }
    void close() noexcept {
        if (bus) {
            if (subscription)
                g_dbus_connection_signal_unsubscribe(bus, subscription);
            subscription = 0;
            if (!session.empty()) {
                auto *reply = g_dbus_connection_call_sync(
                    bus, service, session.c_str(), "org.freedesktop.portal.Session", "Close",
                    nullptr, G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
                if (reply)
                    g_variant_unref(reply);
                else {
                    ++metrics.failures;
                    failure = PortalInputFailure::Call;
                }
            }
            // A private connection makes permission lifetime bounded even if Close fails.
            g_dbus_connection_close_sync(bus, nullptr, nullptr);
            g_object_unref(bus);
            bus = nullptr;
        }
        session.clear();
        held.reset();
        buttons.reset();
        state = PortalInputState::Closed;
    }
    std::string token() { return "cloudplayinput" + std::to_string(++counter); }
    GVariant *request(const char *method, GVariant *parameters, const std::string &token,
                      unsigned timeout) {
        Response response;
        const auto path = std::string(desktop) + "/request/" + sender + "/" + token;
        const auto id = g_dbus_connection_signal_subscribe(
            bus, service, "org.freedesktop.portal.Request", "Response", path.c_str(), nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
               GVariant *value, gpointer data) {
                auto &reply = *static_cast<Response *>(data);
                if (!reply.done && g_variant_is_of_type(value, G_VARIANT_TYPE("(ua{sv})"))) {
                    g_variant_get(value, "(u@a{sv})", &reply.code, &reply.results);
                    reply.done = true;
                }
            },
            &response, nullptr);
        auto *reply = g_dbus_connection_call_sync(bus, service, desktop, remote, method, parameters,
                                                  G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE,
                                                  1000, nullptr, nullptr);
        bool valid{};
        if (reply) {
            const char *handle{};
            g_variant_get(reply, "(&o)", &handle);
            valid = path == handle;
            g_variant_unref(reply);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        while (valid && !response.done && state == PortalInputState::Starting &&
               std::chrono::steady_clock::now() < deadline) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        g_dbus_connection_signal_unsubscribe(bus, id);
        if (!valid || !response.done) {
            auto *closed = g_dbus_connection_call_sync(
                bus, service, path.c_str(), "org.freedesktop.portal.Request", "Close", nullptr,
                nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
            if (closed)
                g_variant_unref(closed);
            throw PortalInputError(state == PortalInputState::Revoked ? PortalInputFailure::Revoked
                                   : valid                            ? PortalInputFailure::Timeout
                                           : PortalInputFailure::Protocol);
        }
        if (response.code == 1)
            throw PortalInputError(PortalInputFailure::Denied);
        if (response.code != 0)
            throw PortalInputError(PortalInputFailure::Protocol);
        if (state != PortalInputState::Starting)
            throw PortalInputError(PortalInputFailure::Revoked);
        return g_variant_ref(response.results);
    }
    bool notify(const char *method, GVariant *parameters) noexcept {
        ++metrics.calls;
        auto *reply = g_dbus_connection_call_sync(bus, service, desktop, remote, method, parameters,
                                                  G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NONE, 250,
                                                  nullptr, nullptr);
        if (reply) {
            g_variant_unref(reply);
            return true;
        }
        ++metrics.failures;
        failure = PortalInputFailure::Call;
        return false;
    }
};

PortalInputError::PortalInputError(PortalInputFailure value)
    : std::runtime_error("Remote desktop input permission/session operation failed"),
      reason(value) {}
PortalInputSink::PortalInputSink() : impl_(std::make_unique<Impl>()) {}
PortalInputSink::~PortalInputSink() = default;
PortalInputState PortalInputSink::state() const noexcept { return impl_->state; }
PortalInputFailure PortalInputSink::last_failure() const noexcept { return impl_->failure; }
PortalInputDiagnostics PortalInputSink::diagnostics() const noexcept { return impl_->metrics; }
void PortalInputSink::pump() noexcept { impl_->pump(); }
void PortalInputSink::close() noexcept { impl_->close(); }

void PortalInputSink::start(unsigned consent_timeout_ms) {
    auto &p = *impl_;
    if (p.state != PortalInputState::Idle)
        throw std::logic_error("Input portal instances are single-use");
    if (consent_timeout_ms < 10 || consent_timeout_ms > 120000)
        throw std::invalid_argument("Input consent timeout must be 10..120000 ms");
    p.state = PortalInputState::Starting;
    const ContextScope scope(p.context);
    try {
        const auto *type = std::getenv("XDG_SESSION_TYPE");
        const auto *address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!type || std::string_view(type) != "wayland" || !address || !*address)
            throw PortalInputError(PortalInputFailure::SessionUnavailable);
        const auto flags =
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
        p.bus = g_dbus_connection_new_for_address_sync(address, flags, nullptr, nullptr, nullptr);
        if (!p.bus)
            throw PortalInputError(PortalInputFailure::SessionUnavailable);
        auto *reply = g_dbus_connection_call_sync(
            p.bus, service, desktop, "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", remote, "AvailableDeviceTypes"), G_VARIANT_TYPE("(v)"),
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        if (!reply)
            throw PortalInputError(PortalInputFailure::Unsupported);
        GVariant *devices{};
        g_variant_get(reply, "(v)", &devices);
        const bool supported = g_variant_is_of_type(devices, G_VARIANT_TYPE_UINT32) &&
                               (g_variant_get_uint32(devices) & 3U) == 3U;
        g_variant_unref(devices);
        g_variant_unref(reply);
        if (!supported)
            throw PortalInputError(PortalInputFailure::Unsupported);
        p.sender = g_dbus_connection_get_unique_name(p.bus);
        p.sender.erase(0, 1);
        std::replace(p.sender.begin(), p.sender.end(), '.', '_');
        auto handle = p.token();
        auto *created = p.request("CreateSession", g_variant_new("(@a{sv})", options(handle, true)),
                                  handle, 5000);
        const char *path{};
        const bool valid = g_variant_lookup(created, "session_handle", "&s", &path) &&
                           g_variant_is_object_path(path);
        if (valid)
            p.session = path;
        g_variant_unref(created);
        if (!valid)
            throw PortalInputError(PortalInputFailure::Protocol);
        p.subscription = g_dbus_connection_signal_subscribe(
            p.bus, service, "org.freedesktop.portal.Session", "Closed", p.session.c_str(), nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
               GVariant *, gpointer data) {
                auto &self = *static_cast<Impl *>(data);
                self.state = PortalInputState::Revoked;
                self.failure = PortalInputFailure::Revoked;
            },
            &p, nullptr);
        handle = p.token();
        auto *selected =
            p.request("SelectDevices",
                      g_variant_new("(o@a{sv})", p.session.c_str(), options(handle, false, true)),
                      handle, 5000);
        g_variant_unref(selected);
        handle = p.token();
        auto *started =
            p.request("Start", g_variant_new("(os@a{sv})", p.session.c_str(), "", options(handle)),
                      handle, consent_timeout_ms);
        guint granted{};
        const bool allowed =
            g_variant_lookup(started, "devices", "u", &granted) && (granted & 3U) == 3U;
        g_variant_unref(started);
        p.metrics.granted_devices = granted;
        if (!allowed)
            throw PortalInputError(PortalInputFailure::Denied);
        p.state = PortalInputState::Active;
    } catch (const PortalInputError &error) {
        p.close();
        ++p.metrics.failures;
        p.failure = error.reason;
        p.state = PortalInputState::Failed;
        throw;
    } catch (...) {
        p.close();
        ++p.metrics.failures;
        p.failure = PortalInputFailure::Protocol;
        p.state = PortalInputState::Failed;
        throw;
    }
}

bool PortalInputSink::apply(const Payload &payload) noexcept {
    auto &p = *impl_;
    p.pump();
    if (p.state != PortalInputState::Active)
        return false;
    if (const auto *key = std::get_if<KeyAction>(&payload)) {
        const auto code = evdev_key(key->usage);
        if (!code) {
            ++p.metrics.invalid_payloads;
            return false;
        }
        bool other_held{};
        for (std::size_t usage = 0; usage < p.held.size(); ++usage)
            if (usage != key->usage && p.held[usage] &&
                evdev_key(static_cast<std::uint16_t>(usage)) == code)
                other_held = true;
        if (other_held || p.held[key->usage] == key->pressed) {
            p.held.set(key->usage, key->pressed);
            ++p.metrics.suppressed_edges;
            return true;
        }
        if (key->pressed)
            p.held.set(key->usage);
        const bool ok = p.notify("NotifyKeyboardKeycode",
                                 g_variant_new("(o@a{sv}iu)", p.session.c_str(), empty_options(),
                                               *code, key->pressed ? 1U : 0U));
        if (ok && !key->pressed)
            p.held.reset(key->usage);
        return ok;
    }
    if (const auto *button = std::get_if<MouseButtonAction>(&payload)) {
        const auto code = evdev_button(button->button);
        if (!code) {
            ++p.metrics.invalid_payloads;
            return false;
        }
        const auto index = button->button - 1;
        if (p.buttons[index] == button->pressed) {
            ++p.metrics.suppressed_edges;
            return true;
        }
        if (button->pressed)
            p.buttons.set(index);
        const bool ok = p.notify("NotifyPointerButton",
                                 g_variant_new("(o@a{sv}iu)", p.session.c_str(), empty_options(),
                                               *code, button->pressed ? 1U : 0U));
        if (ok && !button->pressed)
            p.buttons.reset(index);
        return ok;
    }
    const auto &motion = std::get<MouseMotion>(payload);
    if (motion.dx < -4096 || motion.dx > 4096 || motion.dy < -4096 || motion.dy > 4096) {
        ++p.metrics.invalid_payloads;
        return false;
    }
    return p.notify("NotifyPointerMotion",
                    g_variant_new("(o@a{sv}dd)", p.session.c_str(), empty_options(),
                                  static_cast<double>(motion.dx), static_cast<double>(motion.dy)));
}
} // namespace cloudplay::input
