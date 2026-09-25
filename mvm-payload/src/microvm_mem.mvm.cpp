#include "microvm_rt.h"
#include <intrin.h>

namespace mvm
{

extern MicroVmConfig* g_config;

static void rt_memcpy(void* dst, const void* src, uint64_t n)
{
    __movsb(static_cast<unsigned char*>(dst), static_cast<const unsigned char*>(src), static_cast<size_t>(n));
}

uint64_t TranslateVA(uint64_t cr3, uint64_t va)
{
    uint64_t       table     = cr3 & PFN_MASK;
    const uint32_t shifts[4] = { 39, 30, 21, 12 };

    for (uint32_t level = 0; level < 4; ++level)
    {
        uint64_t index     = (va >> shifts[level]) & 0x1FF;
        auto*    entry_ptr = static_cast<uint64_t*>(PhysToVirt(table + index * 8));
        uint64_t entry     = *entry_ptr;

        if (!(entry & PTE_PRESENT))
            return 0;

        if (level > 0 && level < 3 && (entry & PTE_LARGE))
        {
            uint64_t page_mask = (1ULL << shifts[level]) - 1;
            return (entry & PFN_MASK & ~page_mask) | (va & page_mask);
        }

        table = entry & PFN_MASK;
    }

    return table | (va & PAGE_MASK_4K);
}

bool ReadPhysical(uint64_t pa, void* dest, uint64_t size)
{
    rt_memcpy(dest, PhysToVirt(pa), size);
    return true;
}

bool WritePhysical(uint64_t pa, const void* src, uint64_t size)
{
    rt_memcpy(PhysToVirt(pa), src, size);
    return true;
}

bool ReadVirtual(uint64_t cr3, uint64_t va, void* dest, uint64_t size)
{
    auto* out = static_cast<uint8_t*>(dest);
    while (size > 0)
    {
        uint64_t pa = TranslateVA(cr3, va);
        if (!pa)
            return false;

        uint64_t page_remaining = PAGE_SIZE_4K - (va & PAGE_MASK_4K);
        uint64_t chunk          = size < page_remaining ? size : page_remaining;

        rt_memcpy(out, PhysToVirt(pa), chunk);
        out  += chunk;
        va   += chunk;
        size -= chunk;
    }
    return true;
}

bool WriteVirtual(uint64_t cr3, uint64_t va, const void* src, uint64_t size)
{
    auto* in = static_cast<const uint8_t*>(src);
    while (size > 0)
    {
        uint64_t pa = TranslateVA(cr3, va);
        if (!pa)
            return false;

        uint64_t page_remaining = PAGE_SIZE_4K - (va & PAGE_MASK_4K);
        uint64_t chunk          = size < page_remaining ? size : page_remaining;

        rt_memcpy(PhysToVirt(pa), in, chunk);
        in   += chunk;
        va   += chunk;
        size -= chunk;
    }
    return true;
}

}
