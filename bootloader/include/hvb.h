

#ifndef HVB_H
#define HVB_H

#include "../net/netlog_cfg.h"
#include "../net/hv_features_cfg.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HVB_MAGIC   0x30425648u

#define HVB_VERSION 6u

#define HVB_TOTAL_SIZE (1536u * 1024u * 1024u)

#define HVB_HEADER_SIZE 0x1000u
#define HVB_IMAGE_SIZE  0x800000u

#define HVB_HEADER_OFFSET 0x0000u
#define HVB_IMAGE_OFFSET  (HVB_HEADER_OFFSET + HVB_HEADER_SIZE)
#define HVB_HEAP_OFFSET   (HVB_IMAGE_OFFSET + HVB_IMAGE_SIZE)
#define HVB_HEAP_SIZE     (HVB_TOTAL_SIZE - HVB_HEAP_OFFSET)

#define HVB_FLAG_LOADER_OK           0x00000001u
#define HVB_FLAG_STAGE_A_RAN         0x00000002u
#define HVB_FLAG_STAGE_B_RAN         0x00000004u
#define HVB_FLAG_DRIVER_ATTACHED     0x00000008u
#define HVB_FLAG_BOOT_TIME_HV_ACTIVE 0x00000010u
#define HVB_FLAG_FAILSAFE            0x80000000u

    typedef unsigned char      hvb_u8;
    typedef unsigned short     hvb_u16;
    typedef unsigned int       hvb_u32;
    typedef unsigned long long hvb_u64;

#pragma pack(push, 1)
    typedef struct hvb_header
    {
        hvb_u32 magic;
        hvb_u32 version;
        hvb_u64 total_size;
        hvb_u64 hvb_physical;

        hvb_u64 image_region_offset;
        hvb_u64 image_region_size;

        hvb_u64 heap_offset;
        hvb_u64 heap_size;

        hvb_u32 entry_rva;
        hvb_u32 flags;
        hvb_u32 loader_build;
        hvb_u32 windows_build;

        hvb_u64 vmm_context_pa;

        hvb_u64 debug_log_file_ptr;

        hvb_u64 fb_base;
        hvb_u64 fb_size;
        hvb_u32 fb_width;
        hvb_u32 fb_height;
        hvb_u32 fb_stride;
        hvb_u32 fb_format;

        netlog_cfg_v4 netlog;

        hv_features_v1 features;
    } hvb_header_t;
#pragma pack(pop)

#ifdef __cplusplus
    static_assert(sizeof(hvb_header_t) == 192, "hvb_header_t layout drift");
#else
_Static_assert(sizeof(hvb_header_t) == 192, "hvb_header_t layout drift");
#endif

#ifdef __cplusplus
    static_assert(HVB_IMAGE_OFFSET >= HVB_HEADER_SIZE, "image overlaps header");
    static_assert(HVB_HEAP_OFFSET >= HVB_IMAGE_OFFSET + HVB_IMAGE_SIZE, "heap overlaps image");
    static_assert(HVB_HEAP_OFFSET + HVB_HEAP_SIZE == HVB_TOTAL_SIZE, "heap does not fill remainder");
#endif

#ifdef __cplusplus
}
#endif

#endif
