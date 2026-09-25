#include "microvm_rt.h"

namespace mvm
{

uint64_t PatternScan(uint64_t cr3, uint64_t start_va, uint64_t size, const uint8_t* pattern, const char* mask)
{
    uint32_t pat_len = 0;
    while (mask[pat_len])
        ++pat_len;
    if (pat_len == 0 || pat_len > size)
        return 0;

    for (uint64_t offset = 0; offset <= size - pat_len; ++offset)
    {
        bool matched = true;
        for (uint32_t j = 0; j < pat_len && matched; ++j)
        {
            if (mask[j] == '?')
                continue;

            uint64_t va = start_va + offset + j;
            uint64_t pa = TranslateVA(cr3, va);
            if (!pa)
            {
                matched = false;
                break;
            }

            auto* byte_ptr = static_cast<uint8_t*>(PhysToVirt(pa));
            if (*byte_ptr != pattern[j])
                matched = false;
        }
        if (matched)
            return start_va + offset;
    }
    return 0;
}

}
