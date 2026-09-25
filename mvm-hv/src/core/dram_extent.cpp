

#include <ntddk.h>
#include <intrin.h>
#include <cstdint>

#include "dram_extent.h"

static bool     s_dram_init = false;
static uint64_t s_tom       = 0;
static uint64_t s_tom2      = 0;

static void dram_limits_init()
{
    if (s_dram_init)
        return;

    constexpr uint32_t kMsrTopMem    = 0xC001001A;
    constexpr uint32_t kMsrTopMem2   = 0xC001001D;
    constexpr uint32_t kMsrSyscfg    = 0xC0010010;
    constexpr uint64_t kSyscfgTom2En = 1ULL << 21;

    constexpr uint64_t kTomMask = 0x000FFFFFFF800000ULL;

    s_tom       = __readmsr(kMsrTopMem) & kTomMask;
    s_tom2      = (__readmsr(kMsrSyscfg) & kSyscfgTom2En) ? (__readmsr(kMsrTopMem2) & kTomMask) : 0;
    s_dram_init = true;
}

extern "C" bool pa_is_dram(uint64_t pa, uint64_t bytes)
{
    dram_limits_init();

    const uint64_t end = pa + bytes;
    if (end < pa)
        return false;

    if (s_tom == 0)
        return false;

    if (end <= s_tom)
        return true;
    if (pa >= 0x100000000ULL && end <= s_tom2)
        return true;
    return false;
}
