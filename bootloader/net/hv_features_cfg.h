

#ifndef HV_FEATURES_CFG_H
#define HV_FEATURES_CFG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HV_FEATURES_VERSION 1u

#pragma pack(push, 1)
    typedef struct hv_features_v1
    {
        uint32_t version;

        uint8_t fb_log;
        uint8_t log_mem;
        uint8_t _pad0[2];

        uint8_t virtualize;
        uint8_t intercept_shutdown;
        uint8_t intercept_pause;
        uint8_t intercept_svm_guard;

        uint8_t intercept_msr_prot;
        uint8_t _pad1[3];

        uint32_t boot_menu_timeout_ms;
        uint8_t  _pad2[4];
    } hv_features_v1;
#pragma pack(pop)

#ifdef __cplusplus
    static_assert(sizeof(hv_features_v1) == 24, "hv_features_v1 size drift");
#else
typedef char _hv_features_v1_size_assert[sizeof(hv_features_v1) == 24 ? 1 : -1];
#endif

    void HvFeaturesDefaults(hv_features_v1* out);

    int HvFeaturesParse(const char* text, size_t len, hv_features_v1* out, char* err, size_t err_cap);

    int HvFeaturesFormat(const hv_features_v1* in, char* buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
