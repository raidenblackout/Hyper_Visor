

#pragma once
#include <cstdint>
#include <intrin.h>

#include "boot_stubs.h"

enum HaltReason : uint32_t
{
    HALT_NONE = 0,

    HALT_HOST_STACK_CANARY     = 0x0100,
    HALT_HOST_STACK_SENTINEL   = 0x0101,
    HALT_HOST_STACK_NULLROOT   = 0x0102,
    HALT_PARAMS_CORRUPT        = 0x0103,
    HALT_HSAVE_DRIFT           = 0x0104,
    HALT_VMCB_CORRUPT_EXITCODE = 0x0105,
    HALT_VMCB_OVERWRITTEN      = 0x0106,
    HALT_DOUBLE_FAULT          = 0x0107,
    HALT_UNHANDLED_EXIT        = 0x0108,
    HALT_SHUTDOWN              = 0x0109,
    HALT_PARK_NON_REPORTING    = 0x010A,

    HALT_HOST_EXCEPTION         = 0x0200,
    HALT_HOST_EXCEPTION_CASCADE = 0x0201,

    HALT_LAPIC_INTERCEPT_FAIL = 0x0300,

    HALT_PCI_HIDE_FAIL = 0x0400,

    HALT_TRACE_A = 0x0500,
    HALT_TRACE_B = 0x0501,
};

extern "C" volatile uint32_t g_halt_reason[64];
extern "C" volatile uint64_t g_halt_tsc[64];
extern "C" volatile uint64_t g_halt_rip[64];

static inline void hv_terminate_record(uint32_t reason)
{
    int regs[4] = {};
    __cpuid(regs, 1);
    const uint32_t cpu = (static_cast<uint32_t>(regs[1]) >> 24) & 0x3F;
    const uint64_t rip = reinterpret_cast<uint64_t>(_ReturnAddress());
    const uint64_t tsc = __rdtsc();
    g_halt_reason[cpu] = reason;
    g_halt_tsc[cpu]    = tsc;
    g_halt_rip[cpu]    = rip;
    boot_stubs::log_mem(
        "HALT: cpu%u reason=0x%x rip=0x%llx tsc=%llu", cpu, reason, (unsigned long long)rip, (unsigned long long)tsc);
}
