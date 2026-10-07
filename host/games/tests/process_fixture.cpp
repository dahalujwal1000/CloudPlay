#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    const std::string_view mode(argv[1]);
    if (mode == "verify")
        return argc == 6 && std::filesystem::current_path() == argv[2] &&
                       std::string_view(argv[3]) == "space arg" &&
                       std::string_view(argv[4]) == "$(touch should-not-exist);*" &&
                       std::string_view(argv[5]).empty()
                   ? 0
                   : 9;
    if (mode == "exit7")
        return 7;
    if (mode == "wait") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 0;
    }
    if (mode == "ignore-term" && argc == 3) {
        std::signal(SIGTERM, SIG_IGN);
        {
            std::ofstream ready(argv[2]);
            ready << "ready";
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 0;
    }
    if (mode == "touch-after" && argc == 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::ofstream file(argv[2]);
        file << "alive";
        return file ? 0 : 3;
    }
    return 2;
}
