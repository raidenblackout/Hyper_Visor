

#include "commands.h"
#include "Hypervisor.h"
#include "MicroVM.h"
#include "mvm_client.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <intrin.h>

extern "C" const uint8_t microvm_payload;
extern "C" const uint8_t microvm_payload_end;

#include "../test_payload.mvm.h"
#include "../test_payload.mvm_key.h"
#include "../bench_payload.mvm.h"
#include "../bench_payload.mvm_key.h"

static uint64_t calibrate_tsc_hz()
{
    LARGE_INTEGER qpf;
    if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0)
        return 0;

    LARGE_INTEGER qpc0, qpc1;
    QueryPerformanceCounter(&qpc0);
    const uint64_t tsc0 = __rdtsc();

    const LONGLONG target = qpc0.QuadPart + qpf.QuadPart / 20;
    do
    {
        QueryPerformanceCounter(&qpc1);
    } while (qpc1.QuadPart < target);

    const uint64_t tsc1        = __rdtsc();
    const double   elapsed_sec = static_cast<double>(qpc1.QuadPart - qpc0.QuadPart) / static_cast<double>(qpf.QuadPart);
    if (elapsed_sec <= 0.0)
        return 0;

    return static_cast<uint64_t>(static_cast<double>(tsc1 - tsc0) / elapsed_sec + 0.5);
}

static const uint8_t* get_real_code_ptr(const uint8_t* ptr)
{
    if (ptr && ptr[0] == 0xE9)
    {
        const int32_t rel32 = *reinterpret_cast<const int32_t*>(ptr + 1);
        return ptr + 5 + rel32;
    }
    return ptr;
}

int cmd_microvm(int, LPWSTR*)
{
    const uint8_t* base_code = get_real_code_ptr(&microvm_payload);
    const uint8_t* code_end  = get_real_code_ptr(&microvm_payload_end);
    uint64_t       code_size = static_cast<uint64_t>(code_end - base_code);

    mvm::Hypervisor hv;
    if (!hv.Connect())
    {
        printf("microvm: hypervisor is not present\n");
        return 1;
    }

    mvm::MicroVM vm(hv, code_size);
    if (!vm.IsValid())
    {
        printf("microvm: failed to create micro-VM instance\n");
        return 1;
    }

    printf("microvm: loading payload (%llu bytes)\n", static_cast<unsigned long long>(code_size));

    if (!vm.LoadPayload(base_code, code_size, 0))
    {
        printf("microvm: failed to load payload\n");
        return 1;
    }

    const int64_t final_result = vm.Run(10000);
    printf("microvm: step result = %lld (0 = in progress, 1 = done, <0 = error)\n",
           static_cast<long long>(final_result));

    if (final_result == 1)
        printf("microvm: SUCCESS - micro-VM executed the payload end-to-end\n");
    else
        printf("microvm: FAIL - micro-VM did not reach VMMCALL_MICROVM_DONE\n");

    return final_result == 1 ? 0 : 1;
}

int cmd_microvm_test(int, LPWSTR*)
{
    const uint8_t* base_code = test_payload_bin;
    uint64_t       code_size = test_payload_bin_size;

    mvm::Hypervisor hv;
    if (!hv.Connect())
    {
        printf("microvm-test: hypervisor is not present\n");
        return 1;
    }

    mvm::MicroVM vm(hv, code_size);
    if (!vm.IsValid())
    {
        printf("microvm-test: failed to create micro-VM instance\n");
        return 1;
    }

    void* mailbox = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mailbox)
    {
        printf("microvm-test: failed to allocate mailbox\n");
        return 1;
    }
    SIZE_T min_ws, max_ws;
    GetProcessWorkingSetSize(GetCurrentProcess(), &min_ws, &max_ws);
    SetProcessWorkingSetSize(GetCurrentProcess(), min_ws + 4096 * 4, max_ws + 4096 * 4);
    VirtualLock(mailbox, 4096);
    RtlZeroMemory(mailbox, 4096);

    printf("microvm-test: loading test payload (%llu bytes)\n", static_cast<unsigned long long>(code_size));

    if (!vm.LoadPayload(base_code,
                        code_size,
                        0,
                        MICROVM_FLAG_PHYSMAP_WRITABLE | mvm::MICROVM_FLAG_PRE_ENCRYPTED,
                        test_payload_bin_key))
    {
        printf("microvm-test: failed to load payload\n");
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    if (!vm.MapMailbox(reinterpret_cast<uint64_t>(mailbox)))
    {
        printf("microvm-test: failed to map mailbox\n");
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    const int64_t result = vm.Run(100000);

    if (result != 1)
    {
        printf("microvm-test: FAIL - micro-VM did not complete (result=%lld)\n", static_cast<long long>(result));
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    struct TestReportLocal
    {
        uint64_t magic, total_tests, passed, failed;
        uint64_t tests[20];
        uint32_t diag_walk_count;
        uint32_t diag_walk_failed_at;
        char     diag_names[8][16];
        uint32_t diag_lookup_pid;
        uint32_t diag_lookups_match;
        char     diag_pid_proc_name[16];
        uint64_t diag_pid_found_cr3;
        uint64_t diag_name_found_cr3;
        uint64_t diag_expected_cr3;
    };
    static const char* test_names[] = {
        "config_magic",        "physmap_base",          "heap_alloc",          "heap_remaining",
        "heap_overflow",       "physmap_read",          "translate_va_self",   "read_write_virtual",
        "find_process_system", "find_process_explorer", "find_process_by_pid", "get_process_base",
        "get_process_peb",     "find_module_ntdll",     "find_module_size",    "find_export_ntdll",
        "pattern_scan",        "mailbox_access",        "read_value_typed",    "write_value_typed",
    };

    auto* report = static_cast<TestReportLocal*>(mailbox);
    if (report->magic != 0x54534554ull)
    {
        printf("microvm-test: FAIL - report magic mismatch (got 0x%llX)\n",
               static_cast<unsigned long long>(report->magic));
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    printf("\n=== Micro-VM Runtime Library Test Suite ===\n\n");
    int exit_code = 0;
    for (int i = 0; i < 20; ++i)
    {
        const char* s = (report->tests[i] == 1) ? "PASS" : (report->tests[i] == 2) ? "FAIL" : "SKIP";
        printf("  [%s] %s\n", s, test_names[i]);
        if (report->tests[i] == 2)
            exit_code = 1;
    }

    printf("\n  %llu passed, %llu failed, %llu total\n\n",
           static_cast<unsigned long long>(report->passed),
           static_cast<unsigned long long>(report->failed),
           static_cast<unsigned long long>(report->total_tests));

    if (report->diag_walk_count > 0)
    {
        printf("\n  --- Process Walk Diagnostics (first %u) ---\n", report->diag_walk_count);
        for (uint32_t i = 0; i < report->diag_walk_count && i < 8; ++i)
        {
            report->diag_names[i][15] = '\0';
            printf("    [%u] \"%s\"\n", i, report->diag_names[i]);
        }
        if (report->diag_walk_failed_at)
            printf("    walk failed at iteration %u\n", report->diag_walk_failed_at);
    }

    if (report->diag_lookup_pid != 0)
    {
        report->diag_pid_proc_name[15]    = '\0';
        const unsigned int       pid      = report->diag_lookup_pid;
        const char*              name     = report->diag_pid_proc_name;
        const unsigned long long expected = report->diag_expected_cr3;
        const unsigned long long by_pid   = report->diag_pid_found_cr3;
        const unsigned long long by_name  = report->diag_name_found_cr3;

        printf("\n  --- Self-Lookup Diagnostics (loader PID %u, \"%s\") ---\n", pid, name);
        printf("    Anchor DTB (expected CR3):   0x%llX\n", expected);
        printf("    FindProcessByPid(%u):     0x%llX  %s\n", pid, by_pid, by_pid == expected ? "MATCH" : "MISMATCH");
        printf(
            "    FindProcessByName(\"%s\"): 0x%llX  %s\n", name, by_name, by_name == expected ? "MATCH" : "MISMATCH");
        printf("    Result: %s\n", report->diag_lookups_match ? "PASS" : "FAIL");
    }

    if (report->failed == 0)
        printf("\nmicrovm-test: ALL TESTS PASSED\n");
    else
        printf("\nmicrovm-test: %llu TESTS FAILED\n", static_cast<unsigned long long>(report->failed));

    VirtualFree(mailbox, 0, MEM_RELEASE);
    return exit_code;
}

int cmd_microvm_bench(int argc, LPWSTR* argv)
{
    uint64_t duration_sec = 5;
    if (argc >= 3 && argv && argv[2])
    {
        wchar_t*      end = nullptr;
        unsigned long v   = wcstoul(argv[2], &end, 10);
        if (end && *end == L'\0')
            duration_sec = v;
    }

    const uint8_t* base_code = bench_payload_bin;
    uint64_t       code_size = bench_payload_bin_size;

    mvm::Hypervisor hv;
    if (!hv.Connect())
    {
        printf("microvm-bench: hypervisor is not present\n");
        return 1;
    }

    mvm::MicroVM vm(hv, code_size);
    if (!vm.IsValid())
    {
        printf("microvm-bench: failed to create micro-VM instance\n");
        return 1;
    }

    void* mailbox = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mailbox)
    {
        printf("microvm-bench: failed to allocate mailbox\n");
        return 1;
    }
    SIZE_T min_ws, max_ws;
    GetProcessWorkingSetSize(GetCurrentProcess(), &min_ws, &max_ws);
    SetProcessWorkingSetSize(GetCurrentProcess(), min_ws + 4096 * 4, max_ws + 4096 * 4);
    VirtualLock(mailbox, 4096);
    RtlZeroMemory(mailbox, 4096);

    if (!vm.LoadPayload(base_code,
                        code_size,
                        0,
                        MICROVM_FLAG_PHYSMAP_WRITABLE | mvm::MICROVM_FLAG_PRE_ENCRYPTED,
                        bench_payload_bin_key))
    {
        printf("microvm-bench: failed to load payload\n");
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    if (!vm.MapMailbox(reinterpret_cast<uint64_t>(mailbox)))
    {
        printf("microvm-bench: failed to map mailbox\n");
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    uint64_t tsc_hz = calibrate_tsc_hz();
    if (tsc_hz == 0)
        tsc_hz = 3'000'000'000ULL;
    const uint64_t tsc_mhz = tsc_hz / 1'000'000ULL;

    constexpr uint64_t MAX_DURATION_SEC = 60;
    if (duration_sec > MAX_DURATION_SEC)
    {
        printf("microvm-bench: clamping duration %llu s -> %llu s\n",
               static_cast<unsigned long long>(duration_sec),
               static_cast<unsigned long long>(MAX_DURATION_SEC));
        duration_sec = MAX_DURATION_SEC;
    }

    const uint64_t duration_tsc               = duration_sec * tsc_hz;
    *static_cast<volatile uint64_t*>(mailbox) = duration_tsc;
    printf("microvm-bench: TSC = %llu Hz (~%llu MHz), duration_tsc = %llu (~%llu s)\n",
           static_cast<unsigned long long>(tsc_hz),
           static_cast<unsigned long long>(tsc_mhz),
           static_cast<unsigned long long>(duration_tsc),
           static_cast<unsigned long long>(duration_sec));

    printf("microvm-bench: running RAW (Win32 RPM/WPM) baseline...\n");
    uint64_t           raw_rw_cycles = 0;
    constexpr uint32_t RAW_N         = 10000;
    {
        HANDLE hSelf = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
        if (hSelf)
        {
            uint64_t scratch = 0;
            SIZE_T   bytes   = 0;
            uint64_t start   = __rdtsc();
            for (uint32_t i = 0; i < RAW_N; ++i)
            {
                uint64_t val = 0;
                ReadProcessMemory(hSelf, &scratch, &val, sizeof(val), &bytes);
                val += 1;
                WriteProcessMemory(hSelf, &scratch, &val, sizeof(val), &bytes);
            }
            raw_rw_cycles = __rdtsc() - start;
            CloseHandle(hSelf);
        }
    }
    uint64_t raw_rw_per_op = RAW_N > 0 ? raw_rw_cycles / RAW_N : 0;

    if (duration_sec > 0)
        printf("microvm-bench: running micro-VM sustained loop for ~%llu s...\n",
               static_cast<unsigned long long>(duration_sec));
    else
        printf("microvm-bench: running micro-VM benchmark (legacy fixed loop)...\n");

    const size_t  max_steps     = 200000ULL * (duration_sec + 5);
    uint64_t      vm_run_start  = __rdtsc();
    const int64_t result        = vm.Run(max_steps);
    uint64_t      vm_run_cycles = __rdtsc() - vm_run_start;
    if (result != 1)
    {
        printf("microvm-bench: FAIL - payload did not complete (result=%lld)\n", static_cast<long long>(result));
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    {
        uint64_t stats[6]     = {};
        uint64_t stats_result = 0;
        if (mvm::do_vmmcall(
                VMMCALL_MICROVM_GET_STATS, vm.Handle(), reinterpret_cast<uint64_t>(stats), 0, &stats_result) &&
            stats_result == 1)
        {
            printf("microvm-bench: exit stats: steps=%llu intr=%llu vintr=%llu done=%llu yield=%llu other=%llu\n",
                   (unsigned long long)stats[0],
                   (unsigned long long)stats[1],
                   (unsigned long long)stats[2],
                   (unsigned long long)stats[3],
                   (unsigned long long)stats[4],
                   (unsigned long long)stats[5]);
        }
    }

    struct BenchResultLocal
    {
        char     label[32];
        uint64_t cycles;
        uint64_t iterations;
    };
    struct BenchReportLocal
    {
        uint64_t         input_duration_tsc;
        uint64_t         _input_pad[3];
        uint64_t         magic;
        uint32_t         num_results;
        uint32_t         _pad;
        BenchResultLocal results[20];
        uint64_t         loop_total_cycles;
        uint64_t         loop_iterations;
        uint64_t         target_cr3;
        uint64_t         target_va;
    };

    auto* report = static_cast<BenchReportLocal*>(mailbox);
    if (report->magic != 0x48434E4542ull)
    {
        printf("microvm-bench: FAIL - report magic mismatch\n");
        VirtualFree(mailbox, 0, MEM_RELEASE);
        return 1;
    }

    auto find_result = [&](const char* prefix) -> uint64_t
    {
        for (uint32_t i = 0; i < report->num_results && i < 20; ++i)
        {
            bool match = true;
            for (int c = 0; prefix[c] && c < 31; ++c)
            {
                if (report->results[i].label[c] != prefix[c])
                {
                    match = false;
                    break;
                }
            }
            if (match && report->results[i].iterations > 0)
                return report->results[i].cycles / report->results[i].iterations;
        }
        return 0;
    };

    uint64_t vm_rw = find_result("MVM read+compute+write");
    (void)find_result;

    uint64_t vm_loop_per_op = report->loop_iterations > 0 ? vm_run_cycles / report->loop_iterations : 0;

    printf("\n");
    printf("  ================================================================\n");
    printf("  === Micro-VM Runtime Library Benchmark                        ===\n");
    printf("  ================================================================\n\n");

    printf("  --- All Operations (inside micro-VM, zero VMEXIT) ---\n\n");
    printf("  %-28s %10s %12s\n", "Operation", "Iters", "Cycles/Op");
    printf("  %-28s %10s %12s\n", "-------------------------", "------", "---------");

    for (uint32_t i = 0; i < report->num_results && i < 20; ++i)
    {
        auto& r         = report->results[i];
        r.label[31]     = '\0';
        uint64_t per_op = r.iterations > 0 ? r.cycles / r.iterations : 0;
        printf("  %-28s %10llu %12llu\n",
               r.label,
               static_cast<unsigned long long>(r.iterations),
               static_cast<unsigned long long>(per_op));
    }

    printf("\n  Micro-VM sustained loop: %llu ops in %llu cycles (%llu cyc/op wall clock)\n",
           static_cast<unsigned long long>(report->loop_iterations),
           static_cast<unsigned long long>(vm_run_cycles),
           static_cast<unsigned long long>(vm_loop_per_op));

    printf("\n  --- Comparison: Read->Compute->Write 8B (sustained loop) ---\n\n");
    printf("  %-34s %12s %10s\n", "Method", "Cycles/Op", "Speedup");
    printf("  %-34s %12s %10s\n", "--------------------------------", "---------", "-------");
    printf("  %-34s %12llu %10s\n",
           "RAW (Win32 RPM+WPM, kernel call)",
           static_cast<unsigned long long>(raw_rw_per_op),
           "1x");
    if (vm_rw > 0)
    {
        double speedup = static_cast<double>(raw_rw_per_op) / static_cast<double>(vm_rw);
        printf("  %-34s %12llu %8.1fx\n",
               "Micro-VM (physmap only, no VMEXIT)",
               static_cast<unsigned long long>(vm_rw),
               speedup);
    }
    if (vm_loop_per_op > 0)
    {
        double speedup = static_cast<double>(raw_rw_per_op) / static_cast<double>(vm_loop_per_op);
        printf("  %-34s %12llu %8.1fx\n",
               "Micro-VM (wall clock, incl steps)",
               static_cast<unsigned long long>(vm_loop_per_op),
               speedup);
    }
    printf("\n  --- Frame Budget: Read->Compute->Write ops per frame ---\n\n");
    printf("  %-6s %12s %12s %12s\n", "FPS", "Budget(cyc)", "RAW", "Micro-VM");
    printf("  %-6s %12s %12s %12s\n", "---", "----------", "--------", "--------");

    uint32_t fps_targets[] = { 60, 144, 240, 360 };
    for (uint32_t fps : fps_targets)
    {
        uint64_t budget  = tsc_hz / fps;
        uint64_t raw_ops = raw_rw_per_op > 0 ? budget / raw_rw_per_op : 0;
        uint64_t vm_ops  = vm_loop_per_op > 0 ? budget / vm_loop_per_op : 0;

        printf("  %-6u %12llu %12llu %12llu\n",
               fps,
               static_cast<unsigned long long>(budget),
               static_cast<unsigned long long>(raw_ops),
               static_cast<unsigned long long>(vm_ops));
    }

    printf("\n  CPU TSC: ~%llu MHz (calibrated vs QueryPerformanceCounter)\n",
           static_cast<unsigned long long>(tsc_mhz));
    printf("  RAW = user-mode direct pointer deref (no hypervisor)\n");
    printf("  MVM = micro-VM sustained loop (wall clock / iterations)\n\n");

    VirtualFree(mailbox, 0, MEM_RELEASE);
    return 0;
}
