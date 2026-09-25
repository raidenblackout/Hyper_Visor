#include "microvm_rt.h"
#include <cstddef>
#include <intrin.h>

struct BenchResult
{
    char     label[32];
    uint64_t cycles;
    uint64_t iterations;
};

struct BenchReport
{
    uint64_t input_duration_tsc;
    uint64_t _input_pad[3];

    uint64_t magic;
    uint32_t num_results;
    uint32_t _pad;

    BenchResult results[20];

    uint64_t loop_total_cycles;
    uint64_t loop_iterations;
    uint64_t target_cr3;
    uint64_t target_va;
};

constexpr uint64_t BENCH_MAGIC = 0x48434E4542ULL;

static inline uint64_t rdtsc()
{
    return __rdtsc();
}

static void set_label(BenchResult& r, const char* s)
{
    auto* dst = r.label;
    int   i   = 0;
    for (; i < 31 && s[i]; ++i)
        dst[i] = s[i];
    dst[i] = '\0';
}

static BenchReport* g_bench = nullptr;

static BenchResult& next_result(const char* name)
{
    BenchResult& r = g_bench->results[g_bench->num_results++];
    r.cycles       = 0;
    r.iterations   = 0;
    set_label(r, name);
    return r;
}

static void bench_translate_va(uint64_t kcr3, uint64_t va)
{
    BenchResult&       r     = next_result("TranslateVA");
    constexpr uint32_t N     = 10000;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile uint64_t pa = mvm::TranslateVA(kcr3, va);
        (void)pa;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_physmap_read_8()
{
    BenchResult&       r     = next_result("Physmap read 8B");
    constexpr uint32_t N     = 100000;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile uint64_t v = *static_cast<volatile uint64_t*>(mvm::PhysToVirt(0x1000));
        (void)v;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_read_virtual_8(uint64_t cr3, uint64_t va)
{
    BenchResult&       r = next_result("ReadVirtual 8B");
    constexpr uint32_t N = 10000;
    uint64_t           buf;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
        mvm::ReadVirtual(cr3, va, &buf, 8);
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_write_virtual_8(uint64_t cr3, uint64_t va)
{
    BenchResult&       r     = next_result("WriteVirtual 8B");
    uint64_t           orig  = mvm::ReadValue<uint64_t>(cr3, va);
    constexpr uint32_t N     = 10000;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
        mvm::WriteVirtual(cr3, va, &orig, 8);
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_read_virtual_4k(uint64_t cr3, uint64_t va)
{
    BenchResult&       r   = next_result("ReadVirtual 4KB");
    constexpr uint32_t N   = 1000;
    uint8_t*           buf = static_cast<uint8_t*>(mvm::Alloc(4096));
    if (!buf)
        return;
    uint64_t page_va = va & ~0xFFFULL;
    uint64_t start   = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
        mvm::ReadVirtual(cr3, page_va, buf, 4096);
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_vm_read_compute_write(uint64_t cr3, uint64_t va)
{
    BenchResult&       r     = next_result("MVM read+compute+write");
    constexpr uint32_t N     = 10000;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        uint64_t val  = mvm::ReadValue<uint64_t>(cr3, va);
        val          += 1;
        mvm::WriteValue<uint64_t>(cr3, va, val);
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_find_pid()
{
    BenchResult&       r     = next_result("FindProcessByPid(4)");
    constexpr uint32_t N     = 100;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile uint64_t c = mvm::FindProcessByPid(4);
        (void)c;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_find_name()
{
    BenchResult&       r     = next_result("FindProcessByName");
    constexpr uint32_t N     = 100;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile uint64_t c = mvm::FindProcessByName("smss.exe");
        (void)c;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_heap_alloc()
{
    BenchResult&       r     = next_result("Alloc 64B");
    constexpr uint32_t N     = 10000;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile void* p = mvm::Alloc(64);
        (void)p;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_find_module(uint64_t cr3)
{
    BenchResult&       r     = next_result("FindModuleBase");
    constexpr uint32_t N     = 50;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile uint64_t b = mvm::FindModuleBase(cr3, "ntdll.dll");
        (void)b;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

static void bench_find_export(uint64_t cr3)
{
    uint64_t ntdll = mvm::FindModuleBase(cr3, "ntdll.dll");
    if (!ntdll)
        return;
    BenchResult&       r     = next_result("FindExport");
    constexpr uint32_t N     = 50;
    uint64_t           start = rdtsc();
    for (uint32_t i = 0; i < N; ++i)
    {
        volatile uint64_t a = mvm::FindExport(cr3, ntdll, "NtClose");
        (void)a;
    }
    r.cycles     = rdtsc() - start;
    r.iterations = N;
}

extern "C" void payload_main(MicroVmConfig* config)
{
    mvm::Init(config);

    void* mbox = mvm::GetMailbox(0);
    if (!mbox)
    {
        mvm::Done();
        return;
    }

    g_bench = static_cast<BenchReport*>(mbox);

    constexpr uint64_t MAX_DURATION_TSC = 6ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL;
    uint64_t           duration_tsc     = g_bench->input_duration_tsc;
    if (duration_tsc > MAX_DURATION_TSC)
        duration_tsc = 0;
    auto*          raw       = reinterpret_cast<uint8_t*>(&g_bench->magic);
    const uint64_t out_bytes = sizeof(BenchReport) - offsetof(BenchReport, magic);
    for (uint64_t i = 0; i < out_bytes; ++i)
        raw[i] = 0;

    g_bench->magic       = BENCH_MAGIC;
    g_bench->num_results = 0;

    uint64_t cr3 = mvm::FindProcessByName("smss.exe");
    if (!cr3)
        cr3 = mvm::FindProcessByPid(4);
    if (!cr3)
    {
        mvm::Done();
        return;
    }

    uint64_t peb       = mvm::GetProcessPeb(cr3);
    uint64_t target_va = peb ? peb + 0x10 : config->eprocess_list_head_va;

    g_bench->target_cr3 = cr3;
    g_bench->target_va  = target_va;

    bench_translate_va(config->kernel_cr3, target_va);
    bench_physmap_read_8();
    bench_read_virtual_8(cr3, target_va);
    bench_write_virtual_8(cr3, target_va);
    bench_read_virtual_4k(cr3, target_va);
    bench_vm_read_compute_write(cr3, target_va);
    bench_find_pid();
    bench_find_name();
    bench_heap_alloc();
    bench_find_module(cr3);
    bench_find_export(cr3);

    {
        uint64_t count = 0;
        uint64_t start = rdtsc();

        if (duration_tsc == 0)
        {
            for (uint64_t i = 0; i < 50000; ++i)
            {
                uint64_t val  = mvm::ReadValue<uint64_t>(cr3, target_va);
                val          += 1;
                mvm::WriteValue<uint64_t>(cr3, target_va, val);
                count++;
            }
        }
        else
        {
            constexpr uint64_t BATCH = 1024;
            for (;;)
            {
                for (uint64_t i = 0; i < BATCH; ++i)
                {
                    uint64_t val  = mvm::ReadValue<uint64_t>(cr3, target_va);
                    val          += 1;
                    mvm::WriteValue<uint64_t>(cr3, target_va, val);
                }
                count += BATCH;
                if (rdtsc() - start >= duration_tsc)
                    break;
            }
        }

        g_bench->loop_total_cycles = rdtsc() - start;
        g_bench->loop_iterations   = count;
    }

    mvm::Done();
}
