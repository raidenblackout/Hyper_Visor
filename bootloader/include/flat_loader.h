

#ifndef MVM_FLAT_LOADER_H
#define MVM_FLAT_LOADER_H

#include "efi/efi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void* memset(void* dest, int value, unsigned __int64 count);
    void* memcpy(void* dest, const void* src, unsigned __int64 count);

#define MVMF_MAGIC       0x464D564Du
#define MVMF_VERSION     1u
#define MVMF_HEADER_SIZE 40u

#pragma pack(push, 1)
    typedef struct mvm_flat_header
    {
        UINT32 magic;
        UINT32 version;
        UINT32 header_size;
        UINT32 image_size;
        UINT32 entry_offset;
        UINT32 reloc_count;
        UINT32 reloc_off;
        UINT32 reserved;
        UINT64 link_base;
    } mvm_flat_header_t;
#pragma pack(pop)

    typedef enum flat_load_status
    {
        FLAT_LOAD_OK = 0,
        FLAT_LOAD_ERR_TOO_SMALL,
        FLAT_LOAD_ERR_MAGIC,
        FLAT_LOAD_ERR_VERSION,
        FLAT_LOAD_ERR_HEADER_SIZE,
        FLAT_LOAD_ERR_IMAGE_TOO_BIG,
        FLAT_LOAD_ERR_IMAGE_RANGE,
        FLAT_LOAD_ERR_RELOC_RANGE,
        FLAT_LOAD_ERR_ENTRY_RANGE,
    } flat_load_status_t;

    static flat_load_status_t flat_load(const void* raw,
                                        UINTN       raw_size,
                                        void*       image_base,
                                        UINTN       image_capacity,
                                        UINT64      image_pa,
                                        UINT32*     out_entry_rva,
                                        UINT32*     out_image_size)
    {
        if (raw_size < sizeof(mvm_flat_header_t))
            return FLAT_LOAD_ERR_TOO_SMALL;

        const mvm_flat_header_t* h = (const mvm_flat_header_t*)raw;
        if (h->magic != MVMF_MAGIC)
            return FLAT_LOAD_ERR_MAGIC;
        if (h->version != MVMF_VERSION)
            return FLAT_LOAD_ERR_VERSION;
        if (h->header_size != MVMF_HEADER_SIZE)
            return FLAT_LOAD_ERR_HEADER_SIZE;

        if (h->image_size == 0 || h->image_size > image_capacity)
            return FLAT_LOAD_ERR_IMAGE_TOO_BIG;

        const UINT64 image_end_on_disk = (UINT64)h->header_size + (UINT64)h->image_size;
        if (image_end_on_disk > raw_size)
            return FLAT_LOAD_ERR_IMAGE_RANGE;

        const UINT64 reloc_bytes    = (UINT64)h->reloc_count * sizeof(UINT32);
        const UINT64 reloc_end      = (UINT64)h->reloc_off + reloc_bytes;
        if (h->reloc_count != 0)
        {
            if (h->reloc_off < image_end_on_disk || reloc_end > raw_size)
                return FLAT_LOAD_ERR_RELOC_RANGE;
        }

        if ((UINT64)h->entry_offset >= h->image_size)
            return FLAT_LOAD_ERR_ENTRY_RANGE;

        UINT8* img = (UINT8*)image_base;

        memset(img, 0, h->image_size);
        memcpy(img, (const UINT8*)raw + h->header_size, h->image_size);

        if (h->reloc_count)
        {
            const INT64   delta   = (INT64)image_pa - (INT64)h->link_base;
            const UINT32* rlist   = (const UINT32*)((const UINT8*)raw + h->reloc_off);
            for (UINT32 i = 0; i < h->reloc_count; ++i)
            {
                UINT32 rva = rlist[i];
                if ((UINT64)rva + sizeof(UINT64) > h->image_size)
                    return FLAT_LOAD_ERR_RELOC_RANGE;
                UINT64* slot = (UINT64*)(img + rva);
                *slot        = (UINT64)((INT64)*slot + delta);
            }
        }

        if (out_entry_rva)
            *out_entry_rva = h->entry_offset;
        if (out_image_size)
            *out_image_size = h->image_size;
        return FLAT_LOAD_OK;
    }

    static inline const char* flat_load_status_str(flat_load_status_t s)
    {
        switch (s)
        {
        case FLAT_LOAD_OK:                  return "ok";
        case FLAT_LOAD_ERR_TOO_SMALL:       return "err:too_small";
        case FLAT_LOAD_ERR_MAGIC:           return "err:magic";
        case FLAT_LOAD_ERR_VERSION:         return "err:version";
        case FLAT_LOAD_ERR_HEADER_SIZE:     return "err:header_size";
        case FLAT_LOAD_ERR_IMAGE_TOO_BIG:   return "err:image_too_big";
        case FLAT_LOAD_ERR_IMAGE_RANGE:     return "err:image_range";
        case FLAT_LOAD_ERR_RELOC_RANGE:     return "err:reloc_range";
        case FLAT_LOAD_ERR_ENTRY_RANGE:     return "err:entry_range";
        default:                            return "err:unknown";
        }
    }

#ifdef __cplusplus
}
#endif

#endif
