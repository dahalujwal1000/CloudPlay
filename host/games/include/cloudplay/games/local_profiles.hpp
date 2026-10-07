#pragma once

#include <cloudplay/games/game_profile.hpp>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace cloudplay::games {

enum class CatalogFailure { Io, Trust, Size, Syntax, Schema, InvalidProfile, DuplicateId };
class CatalogError final : public std::runtime_error {
  public:
    explicit CatalogError(CatalogFailure value)
        : std::runtime_error("Local game profile catalog rejected"), reason(value) {}
    const CatalogFailure reason;
};

// Immutable snapshot of caller-selected, trusted local files; never peer-supplied paths.
class LocalProfileCatalog final {
  public:
    static constexpr std::size_t maximum_profiles = 32;
    static constexpr std::size_t maximum_file_bytes = 65536;
    [[nodiscard]] static LocalProfileCatalog load(std::span<const std::filesystem::path> paths);
    [[nodiscard]] const GameProfile *find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const GameProfile> profiles() const noexcept { return profiles_; }

  private:
    explicit LocalProfileCatalog(std::vector<GameProfile> profiles)
        : profiles_(std::move(profiles)) {}
    std::vector<GameProfile> profiles_;
};
} // namespace cloudplay::games
