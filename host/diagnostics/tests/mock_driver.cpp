#include <nvEncodeAPI.h>

extern "C" NVENCSTATUS NVENCAPI NvEncodeAPIGetMaxSupportedVersion(uint32_t *version) {
#if CLOUDPLAY_MOCK_CASE == 2
    (void)version;
    return NV_ENC_ERR_GENERIC;
#elif CLOUDPLAY_MOCK_CASE == 1
    *version = 0;
    return NV_ENC_SUCCESS;
#else
    *version = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    return NV_ENC_SUCCESS;
#endif
}

#if CLOUDPLAY_MOCK_CASE != 5
extern "C" NVENCSTATUS NVENCAPI NvEncodeAPICreateInstance(NV_ENCODE_API_FUNCTION_LIST *functions) {
    (void)functions;
#if CLOUDPLAY_MOCK_CASE == 3
    return NV_ENC_ERR_GENERIC;
#else
    return NV_ENC_SUCCESS;
#endif
}
#endif
