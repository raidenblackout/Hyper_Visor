

#include <ntddk.h>

#include "vmexit_log_capture.h"
#include "common.h"
#include "svm/vmcb.h"
#include "vmexit_log.h"
#include "vmmcall_handler.h"

extern "C" volatile uint64_t g_last_code[64];
extern "C" volatile uint64_t g_last_grip[64];
extern "C" volatile uint64_t g_last_rax[64];
extern "C" volatile uint64_t g_last_rcx[64];
extern "C" volatile uint64_t g_exit_ticks[64];

void vmexit_log_maybe_capture(HOST_CONTEXT* ctx, uint32_t procNum, uint64_t code, uint64_t guest_rcx)
{
#if HV_FEATURE_VMEXIT_LOG

    const bool is_self_drain = (code == VMEXIT_VMMCALL) && (guest_rcx == vmm::VMMCALL_HV_DRAIN_VMEXIT_LOG);
    if (is_self_drain)
        return;

    const uint64_t guest_rax = ctx->Cpu->GuestVmcb.StateSaveArea.Rax;

    vmexit_log::record(procNum,
                       static_cast<uint32_t>(code & 0xFFFF),
                       ctx->Cpu->GuestVmcb.StateSaveArea.Rip,
                       ctx->Cpu->GuestVmcb.ControlArea.ExitInfo1,
                       ctx->Cpu->GuestVmcb.ControlArea.ExitInfo2,
                       guest_rax,
                       guest_rcx);

    const bool is_microvm_call = (code == VMEXIT_VMMCALL) && (guest_rcx >= vmm::VMMCALL_MICROVM_LOAD) &&
                                 (guest_rcx <= vmm::VMMCALL_MICROVM_GET_STATS);
    if (!is_microvm_call)
        return;

    const uint32_t cpu_count = ctx->Root->HostX64.ProcessorCount;
    const uint32_t limit     = cpu_count < 64 ? cpu_count : 64;
    for (uint32_t c = 0; c < limit; ++c)
    {
        if (c == procNum)
            continue;

        vmexit_log::record(procNum,
                           0xF000u | (c & 0xFFF),
                           g_last_grip[c],
                           g_last_code[c],
                           g_exit_ticks[c],
                           g_last_rax[c],
                           g_last_rcx[c]);
    }
#else
    (void)ctx;
    (void)procNum;
    (void)code;
    (void)guest_rcx;
#endif
}
