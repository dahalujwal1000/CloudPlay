#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <nvEncodeAPI.h>
#include <string_view>

namespace {

class DriverLibrary final {
  public:
    DriverLibrary() : handle_(dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL)) {}
    ~DriverLibrary() {
        if (handle_)
            dlclose(handle_);
    }
    DriverLibrary(const DriverLibrary &) = delete;
    DriverLibrary &operator=(const DriverLibrary &) = delete;

    [[nodiscard]] bool loaded() const noexcept { return handle_ != nullptr; }

    template <typename Function> [[nodiscard]] Function symbol(const char *name) const noexcept {
        if (!handle_)
            return nullptr;
        const auto address = dlsym(handle_, name);
        Function function{};
        // POSIX supplies function addresses through void*; copy without a C++ pointer cast.
        static_assert(sizeof(function) == sizeof(address));
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }

  private:
    void *handle_{};
};

int fail(std::string_view reason, int status = 0) {
    std::cerr << "{\"event\":\"nvenc.readiness\",\"ready\":false,\"reason\":\"" << reason
              << "\",\"nvencStatus\":" << status << "}\n";
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: cloudplay_nvenc_probe\n"
                     "Checks driver API compatibility only; does not verify a GPU encode session.\n";
        return 0;
    }
    if (argc != 1)
        return 2;

    const DriverLibrary driver;
    if (!driver.loaded())
        return fail("driver_library_unavailable");
    const auto get_version = driver.symbol<decltype(&NvEncodeAPIGetMaxSupportedVersion)>(
        "NvEncodeAPIGetMaxSupportedVersion");
    const auto create = driver.symbol<decltype(&NvEncodeAPICreateInstance)>("NvEncodeAPICreateInstance");
    if (!get_version || !create)
        return fail("driver_entry_point_unavailable");

    std::uint32_t supported{};
    const auto version_status = get_version(&supported);
    if (version_status != NV_ENC_SUCCESS)
        return fail("version_query_failed", version_status);
    // GetMaxSupportedVersion uses major << 4 | minor, unlike NVENCAPI_VERSION.
    constexpr auto required = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    std::cout << "{\"event\":\"nvenc.api_version\",\"sdkMajor\":" << NVENCAPI_MAJOR_VERSION
              << ",\"sdkMinor\":" << NVENCAPI_MINOR_VERSION
              << ",\"driverMajor\":" << (supported >> 4)
              << ",\"driverMinor\":" << (supported & 15U) << "}\n";
    if (supported < required)
        return fail("driver_api_too_old");

    NV_ENCODE_API_FUNCTION_LIST functions{};
    functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    const auto create_status = create(&functions);
    if (create_status != NV_ENC_SUCCESS)
        return fail("interface_creation_failed", create_status);
    if (!functions.nvEncOpenEncodeSessionEx || !functions.nvEncGetEncodeGUIDs ||
        !functions.nvEncGetEncodeCaps || !functions.nvEncGetEncodePresetConfigEx ||
        !functions.nvEncInitializeEncoder || !functions.nvEncRegisterResource ||
        !functions.nvEncMapInputResource || !functions.nvEncEncodePicture ||
        !functions.nvEncLockBitstream || !functions.nvEncUnlockBitstream ||
        !functions.nvEncUnmapInputResource || !functions.nvEncUnregisterResource ||
        !functions.nvEncDestroyEncoder)
        return fail("required_function_unavailable");

    std::cout << "{\"event\":\"nvenc.readiness\",\"ready\":true,"
                 "\"scope\":\"driver_api_only\",\"hardwareEncodeVerified\":false}\n";
    return 0;
}
