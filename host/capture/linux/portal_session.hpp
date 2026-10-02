#pragma once

#include <cstdint>
#include <gio/gio.h>
#include <string>

namespace cloudplay::capture {

class PortalSession final {
  public:
    PortalSession();
    ~PortalSession();
    PortalSession(const PortalSession &) = delete;
    PortalSession &operator=(const PortalSession &) = delete;
    int open(bool embedded_cursor = false);
    void pump();
    void close() noexcept;
    [[nodiscard]] std::uint32_t node() const noexcept { return node_; }
    [[nodiscard]] const std::string &serial() const noexcept { return serial_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

  private:
    GVariant *request(const char *method, GVariant *parameters, const std::string &token);
    std::string token();
    GMainContext *context_{};
    GDBusConnection *bus_{};
    std::string session_;
    std::string sender_;
    std::string serial_;
    std::uint32_t node_{};
    unsigned counter_{};
    guint closed_subscription_{};
    bool closed_{};
};

} // namespace cloudplay::capture
