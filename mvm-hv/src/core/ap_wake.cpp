

#include <ntddk.h>
#include <intrin.h>

#include "ap_wake.h"
#include "common.h"
#include "svm/msr.h"
#include "svm/vmcb.h"

static void HandleInitSignal(HOST_CONTEXT* ctx)
{
    auto& s  = ctx->Cpu->GuestVmcb.StateSaveArea;
    auto* gr = ctx->Cpu->States.Guest.Registers;

    const uint64_t oldCr0  = s.Cr0;
    uint64_t       newCr0  = 0;
    newCr0                |= (1ULL << 4);
    newCr0                |= (oldCr0 & (1ULL << 30));
    newCr0                |= (oldCr0 & (1ULL << 29));
    s.Cr0                  = newCr0;

    s.Cr2    = 0;
    s.Cr3    = 0;
    s.Cr4    = 0;
    s.Cpl    = 0;
    s.Rflags = (1ULL << 1);

    s.Efer = EFER_SVME;

    s.Rip        = 0xFFF0;
    s.CsSelector = 0xF000;
    s.CsBase     = 0xFFFF0000;
    s.CsLimit    = 0xFFFF;
    s.CsAttrib   = 0x9B;

#define RESET_SEG(sel, base, lim, attrib) \
    do                                    \
    {                                     \
        s.sel##Selector = 0;              \
        s.sel##Base     = base;           \
        s.sel##Limit    = lim;            \
        s.sel##Attrib   = attrib;         \
    } while (0)
    RESET_SEG(Ds, 0, 0xFFFF, 0x93);
    RESET_SEG(Es, 0, 0xFFFF, 0x93);
    RESET_SEG(Fs, 0, 0xFFFF, 0x93);
    RESET_SEG(Gs, 0, 0xFFFF, 0x93);
    RESET_SEG(Ss, 0, 0xFFFF, 0x93);
#undef RESET_SEG

    s.GdtrBase     = 0;
    s.GdtrLimit    = 0xFFFF;
    s.IdtrBase     = 0;
    s.IdtrLimit    = 0xFFFF;
    s.LdtrSelector = 0;
    s.LdtrBase     = 0;
    s.LdtrLimit    = 0xFFFF;
    s.LdtrAttrib   = 0x82;
    s.TrSelector   = 0;
    s.TrBase       = 0;
    s.TrLimit      = 0xFFFF;
    s.TrAttrib     = 0x8B;

    int cpuidRegs[4] = {};
    __cpuid(cpuidRegs, 1);
    gr->Rax = 0;
    gr->Rdx = static_cast<uint32_t>(cpuidRegs[0]);
    gr->Rbx = 0;
    gr->Rcx = 0;
    gr->Rbp = 0;
    s.Rsp   = 0;
    gr->Rdi = 0;
    gr->Rsi = 0;
    gr->R8  = 0;
    gr->R9  = 0;
    gr->R10 = 0;
    gr->R11 = 0;
    gr->R12 = 0;
    gr->R13 = 0;
    gr->R14 = 0;
    gr->R15 = 0;

    __writedr(0, 0);
    __writedr(1, 0);
    __writedr(2, 0);
    __writedr(3, 0);
    s.Dr6 = 0xFFFF0FF0;
    s.Dr7 = 0x400;

    {
        auto& tc = ctx->Cpu->GuestVmcb.ControlArea.TlbControl;
        tc       = (tc & ~HV_SVM_TLB_CTRL_CMD_MASK) | HV_SVM_TLB_CTRL_CMD_FLUSH_ALL;
    }
    ctx->Cpu->GuestVmcb.ControlArea.VmcbClean &= ~((1UL << 5) | (1UL << 6) | (1UL << 7) | (1UL << 8) | (1UL << 9));
}

static uint8_t WaitForSipi(HOST_CONTEXT* ctx)
{
    const uint32_t cpu    = ctx->Cpu->States.Guest.ProcessorNumber;
    const uint32_t apicId = ctx->Cpu->States.Guest.ApicId;

    (void)cpu;
    (void)apicId;

    uint64_t spins = 0;
    while (_InterlockedCompareExchange(reinterpret_cast<volatile long*>(&ctx->Cpu->States.Guest.ActivityState),
                                       GuestStateActive,
                                       GuestStateSipiIssued) != GuestStateSipiIssued)
    {
        _mm_pause();
        ++spins;
    }

    return ctx->Cpu->States.Guest.SipiVector;
}

static void HandleStartupIpi(HOST_CONTEXT* ctx, uint8_t vector)
{
    auto& s      = ctx->Cpu->GuestVmcb.StateSaveArea;
    s.CsSelector = static_cast<uint16_t>(vector) << 8;
    s.CsBase     = static_cast<uint64_t>(vector) << 12;
    s.Rip        = 0;
}

extern "C" volatile uint32_t g_sipi_count[64];

extern "C" void HandleSecurityException(HOST_CONTEXT* ctx)
{
    const uint32_t cpu = ctx->Cpu->States.Guest.ProcessorNumber;
    if (cpu < 64 && g_sipi_count[cpu] != 0)
    {
        return;
    }
    HandleInitSignal(ctx);
    HandleStartupIpi(ctx, WaitForSipi(ctx));
}
