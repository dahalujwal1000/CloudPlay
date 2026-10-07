#include <array>
#include <cloudplay/games/local_profiles.hpp>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace {
using namespace cloudplay::games;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Local profile assertion failed");
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        auto pattern =
            (std::filesystem::temp_directory_path() / "cloudplay-profiles-XXXXXX").string();
        auto *created = mkdtemp(pattern.data());
        check(created);
        path = created;
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void write(const std::filesystem::path &path, const std::string &data) {
    {
        std::ofstream file(path, std::ios::binary);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        check(file.good());
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                           std::filesystem::perms::owner_write);
}
std::string profile(const std::filesystem::path &directory, std::string_view id = "example") {
    return "[profile]\nversion=1\nid=" + std::string(id) +
           "\nname=Example\nexecutable=/usr/bin/fixture\nworking_directory=" + directory.string() +
           "\narguments=space arg;literal\\;value;$(literal);;\n";
}
LocalProfileCatalog load(const std::filesystem::path &path) {
    return LocalProfileCatalog::load(std::array{path});
}
void rejected(const std::filesystem::path &path, CatalogFailure reason) {
    try {
        static_cast<void>(load(path));
        throw std::runtime_error("Bad profile accepted");
    } catch (const CatalogError &error) {
        check(error.reason == reason);
    }
}
} // namespace

int main() {
    Directory directory;
    const auto file = directory.path / "game.ini";
    const auto valid = profile(directory.path);
    write(file, valid);
    const auto catalog = load(file);
    check(catalog.profiles().size() == 1 && catalog.find("example") && !catalog.find("missing"));
    const auto &arguments = catalog.find("example")->arguments;
    check(arguments.size() == 4 && arguments[0] == "space arg" && arguments[1] == "literal;value" &&
          arguments[2] == "$(literal)" && arguments[3].empty());
    write(file, profile(directory.path, "replacement"));
    check(catalog.find("example") && !catalog.find("replacement"));
    write(file, valid + "id=ambiguous\n");
    rejected(file, CatalogFailure::Schema);
    write(file, valid + "unexpected=true\n");
    rejected(file, CatalogFailure::Schema);
    write(file, valid + "name[zz]=hidden\n");
    rejected(file, CatalogFailure::Schema);
    write(file, valid + "[other]\nid=other\n");
    rejected(file, CatalogFailure::Schema);
    write(file, "not a key file\n");
    rejected(file, CatalogFailure::Syntax);
    write(file, "[profile]\nversion=1\nid=example\n");
    rejected(file, CatalogFailure::Schema);
    auto bad = valid;
    bad.replace(bad.find("version=1"), 9, "version=2");
    write(file, bad);
    rejected(file, CatalogFailure::Schema);
    bad = valid;
    bad.replace(bad.find("id=example"), 10, "id=../game");
    write(file, bad);
    rejected(file, CatalogFailure::InvalidProfile);
    bad = valid;
    bad.push_back('\0');
    write(file, bad);
    rejected(file, CatalogFailure::Syntax);
    bad = valid;
    bad.push_back(static_cast<char>(0xff));
    write(file, bad);
    rejected(file, CatalogFailure::Syntax);
    write(file, std::string(LocalProfileCatalog::maximum_file_bytes + 1, 'x'));
    rejected(file, CatalogFailure::Size);
    write(file, valid);
    const auto symlink = directory.path / "link.ini";
    write(file, valid + "#" +
                    std::string(LocalProfileCatalog::maximum_file_bytes - valid.size() - 1, 'x'));
    check(load(file).find("example"));
    write(file, valid);
    std::filesystem::create_symlink(file, symlink);
    rejected(symlink, CatalogFailure::Trust);
    rejected(directory.path, CatalogFailure::Trust);
    rejected("relative.ini", CatalogFailure::Trust);
    rejected(directory.path / "missing.ini", CatalogFailure::Io);
    const auto fifo = directory.path / "fifo";
    check(mkfifo(fifo.c_str(), 0600) == 0);
    rejected(fifo, CatalogFailure::Trust);
    std::filesystem::permissions(file, std::filesystem::perms::group_write,
                                 std::filesystem::perm_options::add);
    rejected(file, CatalogFailure::Trust);
    write(file, valid);
    try {
        static_cast<void>(LocalProfileCatalog::load(std::array{file, file}));
        return 1;
    } catch (const CatalogError &error) {
        check(error.reason == CatalogFailure::DuplicateId);
    }
    try {
        static_cast<void>(LocalProfileCatalog::load({}));
        return 1;
    } catch (const CatalogError &error) {
        check(error.reason == CatalogFailure::Size);
    }
    std::vector<std::filesystem::path> files;
    for (std::size_t index = 0; index < LocalProfileCatalog::maximum_profiles; ++index) {
        const auto path = directory.path / (std::to_string(index) + ".ini");
        write(path, profile(directory.path, "game" + std::to_string(index)));
        files.push_back(path);
    }
    check(LocalProfileCatalog::load(files).profiles().size() == 32);
    files.push_back(file);
    try {
        static_cast<void>(LocalProfileCatalog::load(files));
        return 1;
    } catch (const CatalogError &error) {
        check(error.reason == CatalogFailure::Size);
    }
    // A failed reload cannot mutate a previously loaded catalog.
    write(file, "broken");
    rejected(file, CatalogFailure::Syntax);
    check(catalog.find("example"));
}
