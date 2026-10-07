#include <cloudplay/games/local_profiles.hpp>
#include <iostream>

int main(int argc, char **argv) {
    using namespace cloudplay::games;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: cloudplay_linux_profile_probe --file /absolute/profile.ini [--file "
                     "/absolute/other.ini]\n"
                     "Validate local profile files only; nothing is launched.\n";
        return 0;
    }
    std::vector<std::filesystem::path> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--file" || ++i >= argc ||
            paths.size() == LocalProfileCatalog::maximum_profiles) {
            std::cerr << "Invalid profile diagnostic options\n";
            return 2;
        }
        paths.emplace_back(argv[i]);
    }
    if (paths.empty()) {
        std::cerr << "No profile files selected\n";
        return 2;
    }
    try {
        const auto catalog = LocalProfileCatalog::load(paths);
        std::cout << "{\"ok\":true,\"profile_ids\":[";
        bool first = true;
        for (const auto &profile : catalog.profiles()) {
            if (!first)
                std::cout << ',';
            std::cout << '"' << profile.id << '"';
            first = false;
        }
        std::cout << "],\"launched\":false}\n";
        return 0;
    } catch (const CatalogError &error) {
        std::cerr << "{\"ok\":false,\"catalog_failure\":" << static_cast<unsigned>(error.reason)
                  << "}\n";
        return 1;
    } catch (const std::exception &) {
        std::cerr << "Profile diagnostic failed\n";
        return 1;
    }
}
