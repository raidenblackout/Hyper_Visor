

#include <ntddk.h>
#include <intrin.h>

#include "common.h"
#include "svm/msr.h"
#include "vmexit_log.h"
#include "svm/vmcb.h"
#include "boot_stubs.h"
#include "vmmcall_handler.h"
#include "hv_platform.h"
#include "ap_wake.h"
#include "dram_extent.h"
#include "host_exception.h"
#include "trace.h"
#include "msr_handler.h"
#include "vmexit_log_capture.h"
#include "net/net_log.h"
#include "fb_panic.h"
#include "hv_toggles.h"
#include "hv_terminate.h"

extern "C" volatile uint32_t g_init_count[64] = {};
extern "C" volatile uint32_t g_sipi_count[64] = {};

extern "C" volatile uint32_t g_virt_entry[64];
extern "C" volatile uint32_t g_virt_skip[64];

extern "C" volatile uint64_t g_last_code[64] = {};
extern "C" volatile uint64_t g_last_grip[64] = {};

extern "C" volatile uint64_t g_last_rax[64] = {};
extern "C" volatile uint64_t g_last_rcx[64] = {};

extern "C" volatile uint64_t g_exit_ticks[64] = {};

extern "C" volatile uint64_t g_last_vmexit_tsc[64] = {};
extern "C" volatile uint64_t g_last_vmrun_tsc[64]  = {};

#if HV_UNIQUE_EXIT_TRACK
namespace exit_hist
{
constexpr uint32_t NUM_SITES = 64;

struct site
{
    volatile uint32_t code;
    uint32_t          pad;
    volatile uint64_t rip;
    volatile uint64_t count;
    uint64_t          dumped;
    volatile uint64_t last_tick;
};

static site g_sites[64][NUM_SITES] = {};

static inline uint32_t hash(uint64_t code, uint64_t)
{
    uint64_t x = code * 0x9E3779B97F4A7C15ull;
    return (uint32_t)((x ^ (x >> 16) ^ (x >> 32)) & (NUM_SITES - 1));
}

static inline void record(uint32_t cpu, uint64_t code, uint64_t rip, uint64_t tick)
{
    if (cpu >= 64)
        return;
    const uint32_t h = hash(code, rip);
    site&          s = g_sites[cpu][h];
    if (s.count == 0)
    {
        s.code      = (uint32_t)code;
        s.rip       = rip;
        s.count     = 1;
        s.last_tick = tick;
        s.dumped    = 1;
        boot_stubs::log_mem("u cpu%u code=0x%x rip=0x%llx FIRST last=%llu tsc=%llu",
                            cpu,
                            (unsigned)code,
                            (unsigned long long)rip,
                            (unsigned long long)tick,
                            (unsigned long long)__rdtsc());
        return;
    }

    s.count++;
    s.last_tick = tick;
}

static void dump(uint32_t cpu_count)
{
    constexpr uint32_t MAX_DUMP    = 96;
    static uint32_t    s_start_cpu = 0;
    uint32_t           emitted     = 0;

    if (cpu_count > 64)
        cpu_count = 64;
    for (uint32_t ci = 0; ci < cpu_count; ++ci)
    {
        const uint32_t c = (s_start_cpu + ci) % cpu_count;
        for (uint32_t i = 0; i < NUM_SITES; ++i)
        {
            if (emitted >= MAX_DUMP)
                goto done;

            site&          s   = g_sites[c][i];
            const uint64_t cnt = s.count;
            if (cnt == 0)
                continue;
            const uint64_t delta = cnt - s.dumped;
            if (delta == 0)
                continue;

            boot_stubs::log_mem("u cpu%u code=0x%x rip=0x%llx cnt=%llu +%llu last=%llu tsc=%llu",
                                c,
                                (unsigned)s.code,
                                (unsigned long long)s.rip,
                                (unsigned long long)cnt,
                                (unsigned long long)delta,
                                (unsigned long long)s.last_tick,
                                (unsigned long long)__rdtsc());
            s.dumped = cnt;
            ++emitted;
        }
    }
done:
    s_start_cpu = (s_start_cpu + 1) % (cpu_count ? cpu_count : 1);
}
}
#endif

struct HOST_VMEXIT_STACK
{
    GUEST_REGISTERS             GuestRegisters;
    HOST_STACK_BASED_PARAMETERS Params;
};

extern "C" void HandleNestedPageFault(HOST_CONTEXT* ctx);
extern "C" void HandleDebugException(HOST_CONTEXT* ctx);
extern "C" void maybe_start_lapic_intercept(HOST_CONTEXT* ctx);

static void HandleVmmCall(HOST_CONTEXT* ctx)
{
    vmm::dispatch_vmmcall(ctx);
}

#if HV_FEATURE_SVM_GUARD

static void HandleGuestSvmInstruction(HOST_CONTEXT* ctx, uint64_t code)
{
    static volatile long s_reported = 0;
    if (_InterlockedIncrement(&s_reported) <= 8)
    {
        [[maybe_unused]] const char* name = (code == VMEXIT_VMLOAD)   ? "VMLOAD"
                           : (code == VMEXIT_VMSAVE) ? "VMSAVE"
                           : (code == VMEXIT_STGI)   ? "STGI"
                                                     : "CLGI";
        (void)0;
        (void)0;
    }
    SvmAdvanceRip(&ctx->Cpu->GuestVmcb, 3);
}
#endif

constexpr uint64_t kMaxArchExitCode = 0x403;

static bool IsPlausibleExitCode(uint64_t code)
{
    return code <= kMaxArchExitCode || code == static_cast<uint64_t>(VMEXIT_INVALID) ||
           code == static_cast<uint64_t>(VMEXIT_BUSY);
}

static void park_non_reporting_cpu(HOST_CONTEXT* ctx, uint64_t code, const char* why)
{
    const uint32_t cpu = ctx->Cpu->States.Guest.ProcessorNumber;
    fb_panic::statusf(cpu + fb_panic::kSlotCpuStatusBase,
                      "CPU%02u PARKED: %s code=0x%llx rip=0x%llx info1=0x%llx",
                      cpu,
                      why,
                      (unsigned long long)code,
                      (unsigned long long)ctx->Cpu->GuestVmcb.StateSaveArea.Rip,
                      (unsigned long long)ctx->Cpu->GuestVmcb.ControlArea.ExitInfo1);
    hv_terminate_record(HALT_PARK_NON_REPORTING);
    net_log::flush();
    _disable();
    for (;;)
    {
        __halt();
    }
}

extern "C" void HandleVmExit(HOST_VMEXIT_STACK* stack)
{
    {
        const uint32_t proc = stack->Params.ProcessorNumber;
        if (proc < 64)
            g_last_vmexit_tsc[proc] = __rdtsc();
    }

    {
        static volatile long s_first_exit[2] = { 0 };
        const uint32_t       proc            = stack->Params.ProcessorNumber;
        if (proc < 64)
        {
            const long bit  = 1L << (proc & 31);
            const long prev = _InterlockedOr(&s_first_exit[proc >> 5], bit);
            if ((prev & bit) == 0)
            {
                boot_stubs::log_mem("first_exit: cpu%u", proc);
                net_log::flush();
            }
        }
    }

    if (stack->Params.Reserved1 != HOST_STACK_PARAMS_SENTINEL)
    {
        fb_panic::reportf("*** HOST STACK CORRUPT: params sentinel=0x%x expected=0x%x ***",
                          (unsigned)stack->Params.Reserved1,
                          (unsigned)HOST_STACK_PARAMS_SENTINEL);
        hv_terminate_record(HALT_HOST_STACK_SENTINEL);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    ROOT_CONTEXT* const root = stack->Params.SharedContext;
    if (root == nullptr)
    {
        fb_panic::report("*** HOST STACK CORRUPT: SharedContext is null ***");
        hv_terminate_record(HALT_HOST_STACK_NULLROOT);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    const uint32_t procNum = stack->Params.ProcessorNumber;
    if (procNum >= root->HostX64.ProcessorCount)
    {
        fb_panic::reportf(
            "*** PARAMS CORRUPT: proc=%u >= count=%u (root=%p) ***", procNum, root->HostX64.ProcessorCount, root);
        hv_terminate_record(HALT_PARAMS_CORRUPT);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    HOST_CONTEXT ctx                = {};
    ctx.Root                        = root;
    ctx.Cpu                         = &root->Cpus[procNum];
    ctx.Cpu->States.Guest.Registers = &stack->GuestRegisters;

    if (*reinterpret_cast<const uint64_t*>(&ctx.Cpu->HostStack.Raw[0]) != HOST_STACK_CANARY)
    {
        fb_panic::reportf("*** HOST STACK OVERFLOW: cpu%u floor canary destroyed ***", procNum);
        hv_terminate_record(HALT_HOST_STACK_CANARY);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

#if HV_FEATURE_SVM_GUARD

    {
        const uint64_t expected = reinterpret_cast<uint64_t>(&ctx.Cpu->HostStateArea[0]);
        const uint64_t actual   = __readmsr(0xC0010117);
        if (actual != expected)
        {
            static volatile long s_hsave_drift = 0;
            if (_InterlockedIncrement(&s_hsave_drift) <= 8)
            {
                (void)0;
                (void)0;
            }
            __writemsr(0xC0010117, expected);
        }
    }
#endif

    stack->GuestRegisters.Rax = ctx.Cpu->GuestVmcb.StateSaveArea.Rax;
    __svm_vmload(reinterpret_cast<size_t>(&ctx.Cpu->HostVmcb));

    maybe_start_lapic_intercept(&ctx);

    const uint64_t code = ctx.Cpu->GuestVmcb.ControlArea.ExitCode;

    if (procNum < 64)
    {
        g_last_code[procNum] = code;
        g_last_grip[procNum] = ctx.Cpu->GuestVmcb.StateSaveArea.Rip;
        g_last_rax[procNum]  = ctx.Cpu->GuestVmcb.StateSaveArea.Rax;
        g_last_rcx[procNum]  = stack->GuestRegisters.Rcx;
        ++g_exit_ticks[procNum];
    }

    vmexit_log_maybe_capture(&ctx, procNum, code, stack->GuestRegisters.Rcx);

#if HV_UNIQUE_EXIT_TRACK

    exit_hist::record(procNum, code, ctx.Cpu->GuestVmcb.StateSaveArea.Rip, g_exit_ticks[procNum]);
#endif

    if (!IsPlausibleExitCode(code))
    {
        if (!owns_report_band(procNum))
            park_non_reporting_cpu(&ctx, code, "vmcb corrupt");
        fb_panic::reportf(
            "*** VMCB CORRUPT: cpu%u exitcode=0x%llx is not a valid SVM exit ***", procNum, (unsigned long long)code);
        hv_terminate_record(HALT_VMCB_CORRUPT_EXITCODE);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    {
        const auto& c = ctx.Cpu->GuestVmcb.ControlArea;

        const uint32_t expectMisc1     = g_intercept_misc1;
        const uint32_t expectMisc2     = g_intercept_misc2;
        const uint32_t expectExcpFixed = g_intercept_exception;

        if ((c.InterceptException & expectExcpFixed) != expectExcpFixed || c.InterceptMisc1 != expectMisc1 ||
            c.InterceptMisc2 != expectMisc2 || c.GuestAsid != (procNum + 1))
        {
            if (!owns_report_band(procNum))
                park_non_reporting_cpu(&ctx, code, "vmcb overwritten");
            fb_panic::reportf("*** VMCB OVERWRITTEN: cpu%u -- control area no longer ours ***", procNum);
            fb_panic::reportf("  expect excp=0x%x m1=0x%x m2=0x%x asid=%u",
                              (unsigned)expectExcpFixed,
                              (unsigned)expectMisc1,
                              (unsigned)expectMisc2,
                              (unsigned)(procNum + 1));
            fb_panic::reportf("  actual excp=0x%x m1=0x%x m2=0x%x asid=%u",
                              (unsigned)c.InterceptException,
                              (unsigned)c.InterceptMisc1,
                              (unsigned)c.InterceptMisc2,
                              (unsigned)c.GuestAsid);
            hv_terminate_record(HALT_VMCB_OVERWRITTEN);
            net_log::flush();
            _disable();
            for (;;)
            {
                __halt();
            }
        }
    }

    {
        static volatile uint32_t s_flush_tick = 0;
        if ((_InterlockedIncrement(reinterpret_cast<volatile long*>(&s_flush_tick)) & 0xFF) == 0)
        {
#if HV_UNIQUE_EXIT_TRACK

            exit_hist::dump(64);
#endif
            net_log::flush();
            fb_panic::restore_if_wiped();
        }
    }

    trace_record(&ctx, code);

    {
        static volatile long s_first_exit_bitmap[8] = { 0 };
        if (procNum < 256)
        {
            const long bit  = 1L << (procNum & 31);
            const long prev = _InterlockedOr(&s_first_exit_bitmap[procNum >> 5], bit);
            if ((prev & bit) == 0)
            {
                (void)0;
            }
        }
    }

    switch (code)
    {
    case VMEXIT_VMMCALL:
        HandleVmmCall(&ctx);
        break;

#if HV_FEATURE_SVM_GUARD
    case VMEXIT_VMLOAD:
    case VMEXIT_VMSAVE:
    case VMEXIT_STGI:
    case VMEXIT_CLGI:
        HandleGuestSvmInstruction(&ctx, code);
        break;
#endif

    case VMEXIT_EXCEPTION_SX:
        HandleSecurityException(&ctx);
        break;

    case VMEXIT_EXCEPTION_DB:

        if (!trace_step_consume_db(&ctx))
        {
            HandleDebugException(&ctx);
        }
        break;

    case (0x40 + 8):
        if (!owns_report_band(ctx.Cpu->States.Guest.ProcessorNumber))
            park_non_reporting_cpu(&ctx, code, "df");
        fb_panic::reportf("df: cpu%u DOUBLE-FAULT err=0x%llx rip=0x%llx cs_sel=0x%x cr2=0x%llx",
                          ctx.Cpu->States.Guest.ProcessorNumber,
                          (unsigned long long)ctx.Cpu->GuestVmcb.ControlArea.ExitInfo1,
                          (unsigned long long)ctx.Cpu->GuestVmcb.StateSaveArea.Rip,
                          (unsigned)ctx.Cpu->GuestVmcb.StateSaveArea.CsSelector,
                          (unsigned long long)__readcr2());

        hv_terminate_record(HALT_DOUBLE_FAULT);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }

    case VMEXIT_NPF:
        HandleNestedPageFault(&ctx);
        break;

    case VMEXIT_MSR:
        HandleMsrExit(&ctx);
        break;

    case VMEXIT_PAUSE:

        SvmAdvanceRip(&ctx.Cpu->GuestVmcb, 2);
        break;

    case VMEXIT_SHUTDOWN:

        if (!owns_report_band(ctx.Cpu->States.Guest.ProcessorNumber))
            park_non_reporting_cpu(&ctx, code, "shutdown");
        fb_panic::reportf("*** SHUTDOWN: cpu%u rip=0x%llx (guest would triple-fault) ***",
                          ctx.Cpu->States.Guest.ProcessorNumber,
                          (unsigned long long)ctx.Cpu->GuestVmcb.StateSaveArea.Rip);
        hv_terminate_record(HALT_SHUTDOWN);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }

    default:

        if (!owns_report_band(ctx.Cpu->States.Guest.ProcessorNumber))
            park_non_reporting_cpu(&ctx, code, "unhandled");

        fb_panic::reportf("vmexit: UNHANDLED code=0x%llx cpu%u rip=0x%llx info1=0x%llx info2=0x%llx",
                          (unsigned long long)code,
                          ctx.Cpu->States.Guest.ProcessorNumber,
                          (unsigned long long)ctx.Cpu->GuestVmcb.StateSaveArea.Rip,
                          (unsigned long long)ctx.Cpu->GuestVmcb.ControlArea.ExitInfo1,
                          (unsigned long long)ctx.Cpu->GuestVmcb.ControlArea.ExitInfo2);

        if (hv_platform::system_table() != nullptr)
        {
            boot_stubs::log("vmexit: UNHANDLED code=0x%llx cpu%u rip=0x%llx info1=0x%llx info2=0x%llx",
                            (unsigned long long)code,
                            ctx.Cpu->States.Guest.ProcessorNumber,
                            (unsigned long long)ctx.Cpu->GuestVmcb.StateSaveArea.Rip,
                            (unsigned long long)ctx.Cpu->GuestVmcb.ControlArea.ExitInfo1,
                            (unsigned long long)ctx.Cpu->GuestVmcb.ControlArea.ExitInfo2);
        }
        else
        {
            (void)0;
        }

        hv_terminate_record(HALT_UNHANDLED_EXIT);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    ctx.Cpu->GuestVmcb.StateSaveArea.Rax = stack->GuestRegisters.Rax;

    if (procNum < 64)
        g_last_vmrun_tsc[procNum] = __rdtsc();
}
