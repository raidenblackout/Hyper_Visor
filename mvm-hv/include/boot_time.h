

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    __declspec(dllexport) uint64_t hvruntime_boot_entry(void* SystemTable, uint64_t hvb_pa);

#ifdef __cplusplus
}
#endif
