

#include <ntddk.h>
#include <intrin.h>

#include "common.h"
#include "svm/lapic.h"
#include "svm/msr.h"
#include "svm/vmcb.h"
#include "boot_stubs.h"
#include "fb_panic.h"
#include "net/net_log.h"
#include "net/pci_hide.h"
#include "hv_terminate.h"

extern "C" volatile uint32_t g_remaining_sipi_count = 0;

extern "C" volatile uint32_t g_init_count[64];
extern "C" volatile uint32_t g_sipi_count[64];

extern "C" bool g_lapic_intercept_started = false;

static volatile bool g_lapic_intercept_finished = false;

static volatile long g_lapic_fault_count = 0;

static constexpr long LAPIC_FAULT_BUDGET = 4096;

static uint64_t get_lapic_base()
{
    return __readmsr(0x1B) & ~0xFFFULL;
}

extern "C" void split_2mb_pde(PD_ENTRY_2MB* pde, PT_ENTRY_4KB* pt)
{
    const uint64_t base_pa = static_cast<uint64_t>(pde->Bits.PageFrameNumber) << SVM_LARGE_PAGE_SHIFT;

    uint64_t pa = base_pa;
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRY_COUNT; ++i)
    {
        pt[i].Uint64      = pa;
        pt[i].Bits.Valid  = 1;
        pt[i].Bits.Write  = 1;
        pt[i].Bits.User   = 1;
        pa               += HV_PAGE_SIZE;
    }

    pde->Uint64         = reinterpret_cast<uint64_t>(pt);
    pde->Bits.Valid     = 1;
    pde->Bits.Write     = 1;
    pde->Bits.User      = 1;
    pde->Bits.LargePage = 0;
}

extern "C" PT_ENTRY_4KB* setup_bsp_lapic_pte(ROOT_CONTEXT* root)
{
    const uint64_t             lapic_base = get_lapic_base();
    ADDRESS_TRANSLATION_HELPER h;
    h.AsUInt64 = lapic_base;

    PD_ENTRY_2MB* pde = &root->Svm.NestedPageTablesForBsp.Pd[h.AsIndex.Pml4][h.AsIndex.Pdpt][h.AsIndex.Pd];

    split_2mb_pde(pde, &root->Svm.NestedPtForBsp[0]);

    return &root->Svm.NestedPtForBsp[h.AsIndex.Pt];
}

static void lapic_passthrough_enable(HOST_CONTEXT* ctx, bool enable)
{
    const uint64_t addr =
        enable ? get_lapic_base() : reinterpret_cast<uint64_t>(&ctx->Root->Svm.ShadowLocalApicPage[0]);
    ctx->Cpu->States.Svm.LocalApicNestedPte->Bits.PageFrameNumber = addr >> SVM_PAGE_SHIFT;

    {
        auto& tc = ctx->Cpu->GuestVmcb.ControlArea.TlbControl;
        tc       = (tc & ~HV_SVM_TLB_CTRL_CMD_MASK) | HV_SVM_TLB_CTRL_CMD_FLUSH_ALL;
    }
}

static void lapic_wp_enable(PER_CPU_DATA* cpu, bool enable)
{
    cpu->States.Svm.LocalApicNestedPte->Bits.Write = enable ? 0 : 1;

    auto& tc = cpu->GuestVmcb.ControlArea.TlbControl;
    tc       = (tc & ~HV_SVM_TLB_CTRL_CMD_MASK) | HV_SVM_TLB_CTRL_CMD_FLUSH_ALL;
}

static void lapic_intercept_finish(HOST_CONTEXT* ctx, const char* reason)
{
    (void)reason;
#if HV_FEATURE_LAPIC_AUTODISABLE

    if (g_lapic_intercept_finished)
        return;
    g_lapic_intercept_finished = true;
    lapic_wp_enable(ctx->Cpu, false);
    (void)0;
#else

    (void)ctx;
#endif
}

static void single_step_enable(PER_CPU_DATA* cpu, bool enable)
{
    if (enable)
    {
        cpu->GuestVmcb.StateSaveArea.Rflags           |= (1ULL << 8);
        cpu->GuestVmcb.ControlArea.InterceptException |= (1UL << 1);
    }
    else
    {
        cpu->GuestVmcb.StateSaveArea.Rflags           &= ~(1ULL << 8);
        cpu->GuestVmcb.ControlArea.InterceptException &= ~(1UL << 1);
    }

    cpu->GuestVmcb.ControlArea.VmcbClean &= ~(1UL << 0);
}

static uint32_t apic_id_to_proc_num(const ROOT_CONTEXT* root, uint32_t apicId)
{
    for (uint32_t i = 0; i < root->HostX64.ProcessorCount; ++i)
    {
        if (root->Cpus[i].States.Guest.ApicId == apicId)
            return i;
    }
    return 0xFFFFFFFFu;
}

static void emulate_icr_access(HOST_CONTEXT* ctx)
{
    const uint32_t icr_low = *reinterpret_cast<uint32_t*>(&ctx->Root->Svm.ShadowLocalApicPage[XAPIC_ICR_LOW_OFFSET]);
    const uint32_t mode    = (icr_low & ICR_DELIVERY_MODE_MASK) >> ICR_DELIVERY_MODE_SHIFT;

    if (mode == LOCAL_APIC_DELIVERY_MODE_INIT || mode == LOCAL_APIC_DELIVERY_MODE_STARTUP)
    {
        const uint32_t icr_high     = *reinterpret_cast<volatile uint32_t*>(get_lapic_base() + XAPIC_ICR_HIGH_OFFSET);
        const uint32_t dest_apic_id = icr_high >> 24;
        const uint32_t dest_proc    = apic_id_to_proc_num(ctx->Root, dest_apic_id);

        if (dest_proc != 0xFFFFFFFFu)
        {
            PER_CPU_DATA* dest = &ctx->Root->Cpus[dest_proc];

            if (mode == LOCAL_APIC_DELIVERY_MODE_INIT)
            {
                if (dest_proc < 64)
                    g_init_count[dest_proc]++;

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
                    lapic_intercept_finish(ctx, "sipi-counter-drained");
                }
            }
        }
    }

    *reinterpret_cast<volatile uint32_t*>(get_lapic_base() + XAPIC_ICR_LOW_OFFSET) = icr_low;
}

extern "C" void HandleNestedPageFault(HOST_CONTEXT* ctx)
{
    const uint64_t fault_addr = ctx->Cpu->GuestVmcb.ControlArea.ExitInfo2;

    if (pci_hide::is_hidden(fault_addr))
    {
        pci_hide::emulate(ctx, fault_addr);
        return;
    }

    const uint64_t fault_page   = fault_addr & ~(HV_PAGE_SIZE - 1);
    const uint64_t fault_offset = fault_addr & (HV_PAGE_SIZE - 1);

    if (fault_page != LOCAL_APIC_BASE)
    {
        const uint64_t exitinfo1 = ctx->Cpu->GuestVmcb.ControlArea.ExitInfo1;
        const uint64_t guest_rip = ctx->Cpu->GuestVmcb.StateSaveArea.Rip;
        const uint32_t cpu       = ctx->Cpu->States.Guest.ProcessorNumber;

        fb_panic::reportf("npf: UNMAPPED gpa=0x%llx cpu%u rip=0x%llx info1=0x%llx -- HALT",
                          (unsigned long long)fault_addr,
                          cpu,
                          (unsigned long long)guest_rip,
                          (unsigned long long)exitinfo1);

        hv_terminate_record(HALT_LAPIC_INTERCEPT_FAIL);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    const long total = _InterlockedIncrement(&g_lapic_fault_count);
    if (total >= LAPIC_FAULT_BUDGET)
    {
        lapic_intercept_finish(ctx, "fault-budget-exceeded");
    }

    if (fault_offset == XAPIC_ICR_LOW_OFFSET)
    {
        ctx->Cpu->States.Guest.ApicAccessState = ApicAccessPending;
        lapic_passthrough_enable(ctx, false);
    }

    lapic_wp_enable(ctx->Cpu, false);
    single_step_enable(ctx->Cpu, true);
}

extern "C" void HandleDebugException(HOST_CONTEXT* ctx)
{
    single_step_enable(ctx->Cpu, false);

    if (!g_lapic_intercept_finished)
    {
        lapic_wp_enable(ctx->Cpu, true);
    }

    if (ctx->Cpu->States.Guest.ApicAccessState == ApicAccessPending)
    {
        ctx->Cpu->States.Guest.ApicAccessState = ApicAccessPassthrough;
        lapic_passthrough_enable(ctx, true);
        emulate_icr_access(ctx);
    }
}

extern "C" void maybe_start_lapic_intercept(HOST_CONTEXT* ctx)
{
#if HV_FEATURE_LAPIC_INTERCEPT
    if (ctx->Cpu->States.Guest.ProcessorNumber == 0 && !g_lapic_intercept_started && !g_lapic_intercept_finished &&
        g_remaining_sipi_count != 0)
    {
        lapic_wp_enable(ctx->Cpu, true);
        g_lapic_intercept_started = true;
        (void)0;
    }
#else

    (void)ctx;
#endif
}

extern "C" void arm_bsp_lapic_intercept_now(PER_CPU_DATA* cpu)
{
#if HV_FEATURE_LAPIC_INTERCEPT

    if (cpu->States.Guest.ProcessorNumber != 0)
        return;
    if (!cpu->States.Svm.LocalApicNestedPte)
        return;
    if (g_lapic_intercept_started)
        return;
    lapic_wp_enable(cpu, true);
    g_lapic_intercept_started = true;
#else
    (void)cpu;
#endif
}
