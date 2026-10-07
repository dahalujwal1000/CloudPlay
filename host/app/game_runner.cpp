#include <charconv>
#include <chrono>
#include <cloudplay/app/game_session.hpp>
#include <cloudplay/games/linux_game_process.hpp>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }
bool number(std::string_view value, unsigned &result) {
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    return error == std::errc{} && end == value.data() + value.size() && result >= 1 &&
           result <= 300;
}
} // namespace

int main(int argc, char **argv) {
    using namespace cloudplay;
    using namespace cloudplay::games;
    std::vector<std::filesystem::path> files;
    std::string_view id;
    bool launch{};
    unsigned seconds = 30, stop_after{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--help") {
            std::cout
                << "Usage: cloudplay_linux_game_runner --file /absolute/profile.ini --game ID\n"
                   "       [--launch] [--seconds 1..300] [--stop-after 1..300]\n"
                   "Default: validate selected profile only. --launch starts it locally.\n"
                   "--stop-after explicitly requests SIGTERM; no automatic force stop.\n"
                   "A running child is detached when monitoring ends.\n";
            return 0;
        }
        if (option == "--launch") {
            launch = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing option value\n";
            return 2;
        }
        const std::string_view value(argv[++i]);
        if (option == "--file" && files.size() < LocalProfileCatalog::maximum_profiles)
            files.emplace_back(value);
        else if (option == "--game" && id.empty())
            id = value;
        else if (option == "--seconds" && number(value, seconds)) {
        } else if (option == "--stop-after" && number(value, stop_after)) {
        } else {
            std::cerr << "Invalid game runner option\n";
            return 2;
        }
    }
    if (files.empty() || id.empty() || (stop_after && (!launch || stop_after >= seconds))) {
        std::cerr << "Select profile files and an approved ID; stop-after requires launch and must "
                     "precede seconds\n";
        return 2;
    }
    try {
        auto catalog = LocalProfileCatalog::load(files);
        const auto *profile = catalog.find(id);
        if (!profile)
            throw std::invalid_argument("Game profile ID is not approved");
        if (!launch) {
            std::cout << "{\"ok\":true,\"profile_id\":\"" << profile->id
                      << "\",\"launched\":false}\n";
            return 0;
        }
        // The explicit local CLI request supplies consent, not remote authentication.
        app::GameSession game(std::move(catalog), std::make_unique<LinuxGameProcess>());
        game.launch(id);
        std::cout << "{\"event\":\"launched\",\"profile_id\":\"" << id << "\"}\n" << std::flush;
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        const auto begin = std::chrono::steady_clock::now();
        const auto deadline = begin + std::chrono::seconds(seconds);
        auto stop_at = stop_after ? begin + std::chrono::seconds(stop_after) : deadline;
        while (!interrupted && std::chrono::steady_clock::now() < deadline) {
            game.poll();
            if (game.state() == ProcessState::Exited)
                break;
            if (stop_after && std::chrono::steady_clock::now() >= stop_at) {
                static_cast<void>(game.request_stop());
                stop_after = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        game.poll();
        if (game.state() == ProcessState::Running || game.state() == ProcessState::StopRequested)
            static_cast<void>(game.detach());
        const auto exit = game.exit_result();
        const auto metrics = game.diagnostics();
        const bool detached = game.state() == ProcessState::Detached;
        const bool successful =
            !interrupted && exit &&
            (exit->exit_code == 0 || (metrics.stop_requests && exit->signal == SIGTERM));
        std::cout << "{\"ok\":" << (successful ? "true" : "false") << ",\"event\":\""
                  << (detached ? "detached" : "exited") << "\",\"exit_code\":";
        if (exit && exit->exit_code)
            std::cout << *exit->exit_code;
        else
            std::cout << "null";
        std::cout << ",\"signal\":";
        if (exit && exit->signal)
            std::cout << *exit->signal;
        else
            std::cout << "null";
        std::cout << ",\"stop_requests\":" << metrics.stop_requests << "}\n";
        if (detached)
            std::cerr << "Monitoring ended; the launched child may still be running.\n";
        return interrupted ? 130 : detached ? 3 : successful ? 0 : 1;
    } catch (const CatalogError &error) {
        std::cerr << "{\"ok\":false,\"catalog_failure\":" << static_cast<unsigned>(error.reason)
                  << "}\n";
    } catch (const GameProcessError &error) {
        std::cerr << "{\"ok\":false,\"process_failure\":" << static_cast<unsigned>(error.reason)
                  << "}\n";
    } catch (const std::exception &) {
        std::cerr << "Game runner rejected the request\n";
    }
    return 1;
}
