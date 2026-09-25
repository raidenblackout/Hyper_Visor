

#include <ntddk.h>
#include <intrin.h>
#include <cstdint>

#include "trace.h"
#include "common.h"
#include "svm/vmcb.h"
#include "svm/msr.h"
#include "dram_extent.h"
#include "hv_terminate.h"

namespace
{

constexpr uint32_t kCpuStatusSlotBase = 2;

constexpr uint32_t kMaxTracedCpus = 64;
constexpr uint32_t kRingEntries   = 64;
constexpr uint32_t kRipTableSize  = 8;

constexpr uint32_t kRipRingSize  = 16;
constexpr uint32_t kLeafRingSize = 8;

constexpr uint64_t kRepaintInterval = 256;

struct alignas(64) CPU_EXIT_STATS
{
    uint64_t Total;
    uint64_t Cpuid;
    uint64_t Npf;
    uint64_t Msr;
    uint64_t Eoi;
    uint64_t Sx;
    uint64_t Db;
    uint64_t Df;
    uint64_t Vmc;
    uint64_t Other;

    uint64_t RipTable[kRipTableSize];
    uint32_t RipTableNext;
    uint32_t DistinctRips;

    uint32_t RipTableUsed;

    uint32_t SeenMask;

    uint64_t RipRing[kRipRingSize];
    uint32_t RipRingNext;

    uint32_t Stepping;
    uint64_t StepsTaken;

    uint32_t LeafRing[kLeafRingSize];
    uint32_t LeafRingNext;
};

CPU_EXIT_STATS s_stats[kMaxTracedCpus];

struct EXIT_RECORD
{
    uint64_t Rip;
    uint64_t Info1;
    uint64_t Info2;
    uint32_t Cpu;
    uint32_t Code;
};

EXIT_RECORD   s_ring[kRingEntries];
volatile long s_ring_seq = 0;

constexpr uint32_t kMsrRingSize = 8;
uint32_t           s_msr_ring[kMsrRingSize];
uint32_t           s_msr_next;

volatile uint64_t g_freeze_after_exits = 0;

volatile uint64_t g_step_after_cpuid = 0;
volatile uint64_t g_step_budget      = 500000;

constexpr uint32_t kWatchSlots = 4;

struct WATCH_SLOT
{
    uint64_t    Pa;
    const char* Label;
    uint64_t    First;
    uint64_t    Last;
    uint64_t    Changes;
    uint64_t    ChangedAt;
    uint32_t    Valid;
};

WATCH_SLOT s_watch[kWatchSlots] = {
    { 0x785B8478ULL, "sbrun.cachedRT", 0, 0, 0, 0, 0 },
    { 0x785B82F1ULL, "sbrun.initflag", 0, 0, 0, 0, 0 },
    { 0, "gST.BootSvc", 0, 0, 0, 0, 0 },
    { 0, "gST.RunSvc", 0, 0, 0, 0, 0 },
};

void watch_sample(uint64_t totalExits)
{
    for (uint32_t i = 0; i < kWatchSlots; ++i)
    {
        WATCH_SLOT& w = s_watch[i];
        if (w.Pa == 0 || !pa_is_dram(w.Pa, 8))
            continue;

        const uint64_t v = *reinterpret_cast<const volatile uint64_t*>(w.Pa);

        if (!w.Valid)
        {
            w.First = v;
            w.Last  = v;
            w.Valid = 1;
            continue;
        }
        if (v != w.Last)
        {
            ++w.Changes;
            w.ChangedAt = totalExits;
            w.Last      = v;
        }
    }
}

void step_set(PER_CPU_DATA* cpu, bool on)
{
    if (on)
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

enum : uint32_t
{
    kSeenCpuid = 1u << 0,
    kSeenNpf   = 1u << 1,
    kSeenMsr   = 1u << 2,
    kSeenSx    = 1u << 3,
    kSeenDb    = 1u << 4,
    kSeenDf    = 1u << 5,
    kSeenOther = 1u << 6,
    kSeenEoi   = 1u << 7,
    kSeenVmc   = 1u << 8,
};

uint64_t rip_back(const CPU_EXIT_STATS& st, uint32_t k)
{
    return st.RipRing[(st.RipRingNext - 1 - k) & (kRipRingSize - 1)];
}

uint32_t leaf_back(const CPU_EXIT_STATS& st, uint32_t k)
{
    return st.LeafRing[(st.LeafRingNext - 1 - k) & (kLeafRingSize - 1)];
}

#define TRACE_STATUS_FMT                                              \
    "CPU%02u N=%010llu CID=%010llu EOI=%09llu NPF=%06llu MSR=%06llu " \
    "SX=%05llu DB=%05llu DF=%05llu VMC=%05llu OTH=%05llu URIP=%04u "  \
    "RIPS=%llx %llx LF=%x %x %x"

#define TRACE_STATUS_ARGS(cpu, st)                                                                       \
    (cpu), (unsigned long long)(st).Total, (unsigned long long)(st).Cpuid, (unsigned long long)(st).Eoi, \
        (unsigned long long)(st).Npf, (unsigned long long)(st).Msr, (unsigned long long)(st).Sx,         \
        (unsigned long long)(st).Db, (unsigned long long)(st).Df, (unsigned long long)(st).Vmc,          \
        (unsigned long long)(st).Other, (st).DistinctRips, (unsigned long long)rip_back((st), 1),        \
        (unsigned long long)rip_back((st), 0), leaf_back((st), 2), leaf_back((st), 1), leaf_back((st), 0)

void trace_status_paint(uint32_t, const CPU_EXIT_STATS&) {}
void report_ap_accounting() {}
void trace_dump(const char*, uint32_t){}
#if 0
        (void)0;

        report_ap_accounting();

        uint32_t shown = 0;
        for (uint32_t i = 0; i < kMaxTracedCpus && shown < 4; ++i)
        {
            if (s_stats[i].Total == 0) continue;
            (void)0;
            ++shown;
        }

        if (focus_cpu < kMaxTracedCpus && s_stats[focus_cpu].Total != 0)
        {

            const CPU_EXIT_STATS& f = s_stats[focus_cpu];
            constexpr uint32_t kShowRips = 8;
            const uint32_t ringed = (f.RipRingNext < kRipRingSize)
                                  ? f.RipRingNext : kRipRingSize;
            const uint32_t have   = (ringed < kShowRips) ? ringed : kShowRips;

            (void)0;

            for (uint32_t base = 0; base < have; base += 4)
            {
                uint64_t v[4] = {};
                for (uint32_t k = 0; k < 4; ++k)
                {
                    const uint32_t n = base + k;
                    v[k] = (n < have)
                         ? f.RipRing[(f.RipRingNext - have + n) & (kRipRingSize - 1)]
                         : 0;
                }
                (void)0;
            }
        }

        (void)0;

        const long seq   = s_ring_seq;
        long       count = (seq < static_cast<long>(kRingEntries))
                         ? seq : static_cast<long>(kRingEntries);

        if (count > 2) count = 2;

        for (long i = count; i >= 1; --i)
        {
            const EXIT_RECORD& r = s_ring[static_cast<uint32_t>(seq - i) & (kRingEntries - 1)];
            (void)0;
        }
    }
#endif
}

bool trace_step_consume_db(HOST_CONTEXT* ctx)
{
    const uint32_t cpu = ctx->Cpu->States.Guest.ProcessorNumber;
    if (cpu >= kMaxTracedCpus)
        return false;

    CPU_EXIT_STATS& st = s_stats[cpu];
    if (!st.Stepping)
        return false;

    ++st.StepsTaken;

    ctx->Cpu->GuestVmcb.StateSaveArea.Dr6 &= ~(1ULL << 14);

    if (st.StepsTaken >= g_step_budget)
    {
        step_set(ctx->Cpu, false);
        st.Stepping = 0;
    }
    return true;
}

void trace_record(HOST_CONTEXT* ctx, uint64_t code)
{
    const uint32_t cpu = ctx->Cpu->States.Guest.ProcessorNumber;
    if (cpu >= kMaxTracedCpus)
        return;

    CPU_EXIT_STATS& st  = s_stats[cpu];
    const uint64_t  rip = ctx->Cpu->GuestVmcb.StateSaveArea.Rip;

    uint32_t bit = kSeenOther;
    switch (code)
    {
    case VMEXIT_NPF:
        ++st.Npf;
        bit = kSeenNpf;
        break;
    case VMEXIT_EXCEPTION_SX:
        ++st.Sx;
        bit = kSeenSx;
        break;
    case VMEXIT_VMMCALL:
        ++st.Vmc;
        bit = kSeenVmc;
        break;

    case VMEXIT_MSR:
    {
        const uint32_t m                            = static_cast<uint32_t>(ctx->Cpu->States.Guest.Registers->Rcx);
        const bool     wr                           = ctx->Cpu->GuestVmcb.ControlArea.ExitInfo1 == 1;
        s_msr_ring[s_msr_next & (kMsrRingSize - 1)] = m | (wr ? 0x80000000u : 0u);
        ++s_msr_next;
    }

        if (static_cast<uint32_t>(ctx->Cpu->States.Guest.Registers->Rcx) == X2APIC_MSR_EOI)
        {
            ++st.Eoi;
            bit = kSeenEoi;
        }
        else
        {
            ++st.Msr;
            bit = kSeenMsr;
        }
        break;

    case VMEXIT_EXCEPTION_DB:
        ++st.Db;
        bit = kSeenDb;
        break;
    case (0x40 + 8):
        ++st.Df;
        bit = kSeenDf;
        break;
    default:
        ++st.Other;
        break;
    }

    ++st.Total;

    if (cpu == 0)
        watch_sample(st.Total);

    st.RipRing[st.RipRingNext & (kRipRingSize - 1)] = rip;
    ++st.RipRingNext;

    bool hit = st.Stepping != 0;
    for (uint32_t i = 0; !hit && i < st.RipTableUsed; ++i)
    {
        if (st.RipTable[i] == rip)
        {
            hit = true;
            break;
        }
    }
    if (!hit)
    {
        st.RipTable[st.RipTableNext & (kRipTableSize - 1)] = rip;
        ++st.RipTableNext;
        if (st.RipTableUsed < kRipTableSize)
            ++st.RipTableUsed;
        ++st.DistinctRips;
    }

    {
        const long   idx = _InterlockedIncrement(&s_ring_seq) - 1;
        EXIT_RECORD& r   = s_ring[static_cast<uint32_t>(idx) & (kRingEntries - 1)];
        r.Cpu            = cpu;
        r.Code           = static_cast<uint32_t>(code);
        r.Rip            = rip;
        r.Info1          = ctx->Cpu->GuestVmcb.ControlArea.ExitInfo1;
        r.Info2          = ctx->Cpu->GuestVmcb.ControlArea.ExitInfo2;
    }

    if (code == VMEXIT_NPF && st.Stepping)
    {
        step_set(ctx->Cpu, false);
        st.Stepping   = 0;
        st.StepsTaken = st.StepsTaken ? st.StepsTaken : 1;
    }
    else if (!st.Stepping && st.StepsTaken == 0 && cpu == 0 && g_step_after_cpuid != 0 &&
             st.Cpuid >= g_step_after_cpuid)
    {
        step_set(ctx->Cpu, true);
        st.Stepping = 1;
    }

    [[maybe_unused]] const bool first_of_class  = (st.SeenMask & bit) == 0;
    st.SeenMask               |= bit;

    [[maybe_unused]] const uint64_t interval = st.Stepping ? (kRepaintInterval * 32) : kRepaintInterval;

    const uint64_t freeze = g_freeze_after_exits;
    if (freeze != 0 && st.Total >= freeze)
    {
        (void)0;
        hv_terminate_record(HALT_TRACE_A);
        _disable();
        for (;;)
        {
            __halt();
        }
    }
}

extern "C" void HvPublishWatch(uint32_t slot, uint64_t pa)
{
    if (slot < kWatchSlots)
        s_watch[slot].Pa = pa;
}
namespace
{
volatile uint32_t g_report_cpu = 0;
}

bool owns_report_band(uint32_t cpu)
{
    return cpu == g_report_cpu;
}

[[noreturn]] void park_non_reporting_cpu(HOST_CONTEXT*, uint64_t, const char*)
{
    hv_terminate_record(HALT_TRACE_B);
    _disable();
    for (;;)
    {
        __halt();
    }
}
