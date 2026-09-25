

#include <ntddk.h>
#include <intrin.h>

#include "x2apic_icr.h"
#include "common.h"
#include "svm/lapic.h"
#include "svm/msr.h"
#include "svm/vmcb.h"

extern "C" volatile uint32_t g_remaining_sipi_count;
extern "C" volatile uint32_t g_sipi_count[64];

extern "C" long      g_x2apic_wrmsr_budget       = 0;
static volatile bool g_x2apic_intercept_finished = false;
static volatile long g_x2apic_wrmsr_count        = 0;

static uint32_t apic_id_to_proc_num(const ROOT_CONTEXT* root, uint32_t apicId)
{
    for (uint32_t i = 0; i < root->HostX64.ProcessorCount; ++i)
    {
        if (root->Cpus[i].States.Guest.ApicId == apicId)
            return i;
    }
    return 0xFFFFFFFFu;
}

static void x2apic_intercept_finish(HOST_CONTEXT*, const char*) {}

extern "C" void x2apic_emulate_icr_msr(HOST_CONTEXT* ctx)
{
    const long total = _InterlockedIncrement(&g_x2apic_wrmsr_count);
    if (total >= g_x2apic_wrmsr_budget && !g_x2apic_intercept_finished)
    {
        x2apic_intercept_finish(ctx, "wrmsr-budget-exceeded");
    }

    auto*          gr       = ctx->Cpu->States.Guest.Registers;
    const uint32_t icr_low  = static_cast<uint32_t>(gr->Rax);
    const uint32_t icr_high = static_cast<uint32_t>(gr->Rdx);
    const uint32_t mode     = (icr_low & ICR_DELIVERY_MODE_MASK) >> ICR_DELIVERY_MODE_SHIFT;

    if (mode == LOCAL_APIC_DELIVERY_MODE_INIT || mode == LOCAL_APIC_DELIVERY_MODE_STARTUP)
    {
        const uint32_t dest_apic_id = icr_high;
        const uint32_t dest_proc    = apic_id_to_proc_num(ctx->Root, dest_apic_id);

        if (dest_proc != 0xFFFFFFFFu)
        {
            PER_CPU_DATA* dest = &ctx->Root->Cpus[dest_proc];

            if (mode == LOCAL_APIC_DELIVERY_MODE_INIT)
            {
                dest->States.Guest.SipiVector = 0;
                _InterlockedCompareExchange(reinterpret_cast<volatile long*>(&dest->States.Guest.ActivityState),
                                            GuestStateWaitForSipi,
                                            dest->States.Guest.ActivityState);
            }
            else
            {
                if (dest_proc < 64)
                    g_sipi_count[dest_proc]++;
                dest->States.Guest.SipiVector = static_cast<uint8_t>(icr_low & 0xFF);
                _InterlockedCompareExchange(reinterpret_cast<volatile long*>(&dest->States.Guest.ActivityState),
                                            GuestStateSipiIssued,
                                            dest->States.Guest.ActivityState);

                const int32_t left = static_cast<int32_t>(
                    _InterlockedDecrement(reinterpret_cast<volatile long*>(&g_remaining_sipi_count)));
                if (left <= 0)
                {
                    x2apic_intercept_finish(ctx, "sipi-counter-drained");
                }
            }
        }
    }

    __writemsr(X2APIC_MSR_ICR, (static_cast<uint64_t>(icr_high) << 32) | icr_low);

    SvmAdvanceRip(&ctx->Cpu->GuestVmcb, 2);
}
