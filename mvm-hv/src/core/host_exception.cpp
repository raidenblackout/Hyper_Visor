

#include <ntddk.h>
#include <intrin.h>

#include "host_exception.h"
#include "common.h"
#include "dram_extent.h"
#include "hv_platform.h"
#include "boot_stubs.h"
#include "fb_panic.h"
#include "net/net_log.h"
#include "hv_terminate.h"

static void report_ap_accounting() {}

extern "C" void HandleHostException(const HOST_EXCEPTION_STACK* stack)
{
    static volatile long s_reporting = 0;
    if (_InterlockedCompareExchange(&s_reporting, 1, 0) != 0)
    {
        hv_terminate_record(HALT_HOST_EXCEPTION_CASCADE);
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    int cpuidRegs[4] = {};
    __cpuid(cpuidRegs, 1);
    const uint32_t apicId = static_cast<uint32_t>(cpuidRegs[1]) >> 24;

    fb_panic::reportf("host_fault: vec=%llu err=0x%llx rip=0x%llx cs=0x%llx flags=0x%llx apic=%u",
                      (unsigned long long)stack->InterruptNumber,
                      (unsigned long long)stack->ErrorCode,
                      (unsigned long long)stack->Rip,
                      (unsigned long long)stack->Cs,
                      (unsigned long long)stack->Rflags,
                      (unsigned)apicId);

    boot_stubs::log_mem("host_fault: cpu%u vec=%llu err=0x%llx rip=0x%llx cs=0x%llx flags=0x%llx",
                        (unsigned)apicId,
                        (unsigned long long)stack->InterruptNumber,
                        (unsigned long long)stack->ErrorCode,
                        (unsigned long long)stack->Rip,
                        (unsigned long long)stack->Cs,
                        (unsigned long long)stack->Rflags);

    if (stack->InterruptNumber == 14)
    {
        const uint64_t cr2 = __readcr2();
        fb_panic::reportf("host_fault: cr2=0x%llx (P|W err bits decode above)", (unsigned long long)cr2);
        boot_stubs::log_mem("host_fault: cpu%u cr2=0x%llx", (unsigned)apicId, (unsigned long long)cr2);
    }

    {
        const GUEST_REGISTERS& g = stack->GuestRegisters;
        (void)g;
    }
    {
        const uint64_t rsp = stack->Rsp;
        if (rsp != 0 && (rsp % sizeof(uint64_t)) == 0 && pa_is_dram(rsp, 64))
        {
            const volatile uint64_t* s = reinterpret_cast<const volatile uint64_t*>(rsp);
            (void)s;
        }
    }

    report_ap_accounting();

    if (hv_platform::system_table() != nullptr)
    {
        boot_stubs::log("host_fault: vec=%llu err=0x%llx rip=0x%llx cs=0x%llx flags=0x%llx",
                        (unsigned long long)stack->InterruptNumber,
                        (unsigned long long)stack->ErrorCode,
                        (unsigned long long)stack->Rip,
                        (unsigned long long)stack->Cs,
                        (unsigned long long)stack->Rflags);
    }

    hv_terminate_record(HALT_HOST_EXCEPTION);
    net_log::flush();
    _disable();
    for (;;)
    {
        __halt();
    }
}
