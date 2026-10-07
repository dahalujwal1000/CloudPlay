#include <array>
#include <bitset>
#include <cerrno>
#include <cloudplay/games/local_profiles.hpp>
#include <fcntl.h>
#include <glib.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace cloudplay::games {
namespace {
struct File {
    int fd;
    ~File() {
        if (fd >= 0)
            ::close(fd);
    }
};
using KeyFile = std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)>;
using Strings = std::unique_ptr<gchar *, decltype(&g_strfreev)>;
using Text = std::unique_ptr<gchar, decltype(&g_free)>;

std::string read_profile(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.native().find('\0') != std::string::npos)
        throw CatalogError(CatalogFailure::Trust);
    File file{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    if (file.fd < 0)
        throw CatalogError(errno == ELOOP ? CatalogFailure::Trust : CatalogFailure::Io);
    struct stat status {};
    if (fstat(file.fd, &status) != 0)
        throw CatalogError(CatalogFailure::Io);
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() || (status.st_mode & 022) != 0)
        throw CatalogError(CatalogFailure::Trust);
    if (status.st_size < 0 ||
        status.st_size > static_cast<off_t>(LocalProfileCatalog::maximum_file_bytes))
        throw CatalogError(CatalogFailure::Size);
    // Read from the checked descriptor, bounded even if a file grows concurrently.
    std::string data;
    std::array<char, 4096> chunk{};
    for (;;) {
        const auto count = ::read(file.fd, chunk.data(), chunk.size());
        if (count < 0) {
            if (errno == EINTR)
                continue;
            throw CatalogError(CatalogFailure::Io);
        }
        if (count == 0)
            break;
        if (data.size() + static_cast<std::size_t>(count) > LocalProfileCatalog::maximum_file_bytes)
            throw CatalogError(CatalogFailure::Size);
        data.append(chunk.data(), static_cast<std::size_t>(count));
    }
    if (data.find('\0') != std::string::npos ||
        !g_utf8_validate(data.data(), static_cast<gssize>(data.size()), nullptr))
        throw CatalogError(CatalogFailure::Syntax);
    return data;
}

std::string text(GKeyFile *file, const char *key) {
    Text value(g_key_file_get_string(file, "profile", key, nullptr), g_free);
    if (!value)
        throw CatalogError(CatalogFailure::Schema);
    return value.get();
}
GameProfile parse(const std::string &data) {
    KeyFile file(g_key_file_new(), g_key_file_unref);
    if (!g_key_file_load_from_data(file.get(), data.data(), data.size(),
                                   G_KEY_FILE_KEEP_TRANSLATIONS, nullptr))
        throw CatalogError(CatalogFailure::Syntax);
    gsize count{};
    Strings groups(g_key_file_get_groups(file.get(), &count), g_strfreev);
    if (count != 1 || std::string_view(groups.get()[0]) != "profile")
        throw CatalogError(CatalogFailure::Schema);
    Strings keys(g_key_file_get_keys(file.get(), "profile", &count, nullptr), g_strfreev);
    constexpr std::array<std::string_view, 6> allowed{
        "version", "id", "name", "executable", "working_directory", "arguments"};
    std::bitset<allowed.size()> seen;
    for (gsize i = 0; i < count; ++i) {
        std::size_t index{};
        while (index < allowed.size() && allowed[index] != keys.get()[i])
            ++index;
        if (index == allowed.size() || seen[index])
            throw CatalogError(CatalogFailure::Schema);
        seen.set(index);
    }
    if (!seen.all() || g_key_file_get_integer(file.get(), "profile", "version", nullptr) != 1)
        throw CatalogError(CatalogFailure::Schema);
    GameProfile profile{text(file.get(), "id"),
                        text(file.get(), "name"),
                        text(file.get(), "executable"),
                        text(file.get(), "working_directory"),
                        {}};
    GError *error{};
    Strings arguments(
        g_key_file_get_string_list(file.get(), "profile", "arguments", &count, &error), g_strfreev);
    if (error) {
        g_error_free(error);
        throw CatalogError(CatalogFailure::Schema);
    }
    if (count > 64)
        throw CatalogError(CatalogFailure::InvalidProfile);
    for (gsize i = 0; i < count; ++i)
        profile.arguments.emplace_back(arguments.get()[i]);
    if (validate(profile))
        throw CatalogError(CatalogFailure::InvalidProfile);
    return profile;
}
} // namespace

LocalProfileCatalog LocalProfileCatalog::load(std::span<const std::filesystem::path> paths) {
    if (paths.empty() || paths.size() > maximum_profiles)
        throw CatalogError(CatalogFailure::Size);
    std::vector<GameProfile> profiles;
    profiles.reserve(paths.size());
    for (const auto &path : paths) {
        auto profile = parse(read_profile(path));
        for (const auto &previous : profiles)
            if (previous.id == profile.id)
                throw CatalogError(CatalogFailure::DuplicateId);
        profiles.push_back(std::move(profile));
    }
    return LocalProfileCatalog(std::move(profiles));
}
const GameProfile *LocalProfileCatalog::find(std::string_view id) const noexcept {
    for (const auto &profile : profiles_)
        if (profile.id == id)
            return &profile;
    return nullptr;
}
} // namespace cloudplay::games
