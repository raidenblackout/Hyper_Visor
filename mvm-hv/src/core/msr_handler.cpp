

#include <ntddk.h>
#include <intrin.h>

#include "msr_handler.h"
#include "common.h"
#include "svm/msr.h"
#include "svm/vmcb.h"
#include "x2apic_icr.h"

static void inject_gp(HOST_CONTEXT* ctx)
{
    constexpr uint64_t GP_INJECT = (uint64_t(1) << 31) | (uint64_t(1) << 11) | (uint64_t(3) << 8) | (uint64_t(13));
    ctx->Cpu->GuestVmcb.ControlArea.EventInj = GP_INJECT;
}

void HandleMsrExit(HOST_CONTEXT* ctx)
{
    const uint32_t msr   = static_cast<uint32_t>(ctx->Cpu->States.Guest.Registers->Rcx);
    const bool     is_wr = (ctx->Cpu->GuestVmcb.ControlArea.ExitInfo1 == 1);

    if (msr == X2APIC_MSR_ICR && is_wr)
    {
        x2apic_emulate_icr_msr(ctx);
        return;
    }

    if (msr == X2APIC_MSR_EOI && is_wr)
    {
        __writemsr(X2APIC_MSR_EOI, 0);
        SvmAdvanceRip(&ctx->Cpu->GuestVmcb, 2);
        return;
    }

#if HV_FEATURE_SVM_GUARD
    if (msr == 0xC0010117u && is_wr)
    {
        SvmAdvanceRip(&ctx->Cpu->GuestVmcb, 2);
        return;
    }
#endif

    inject_gp(ctx);
}
