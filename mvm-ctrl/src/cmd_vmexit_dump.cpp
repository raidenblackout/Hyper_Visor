

#include "commands.h"
#include "mvm_client.h"

#include <cstdio>
#include <cstdint>

static void write_entry(FILE* fp, const HvVmexitLogEntry& e)
{
    if ((e.code & 0xF000) == 0xF000)
    {
        const unsigned target_cpu = e.code & 0xFFF;
        fprintf(fp,
                "tsc=%016llx observer=cpu%u snapshot(cpu%u) "
                "last_code=0x%04llx rip=%016llx rax=%016llx rcx=%016llx exits=%llu\n",
                (unsigned long long)e.tsc,
                (unsigned)e.cpu,
                target_cpu,
                (unsigned long long)(e.info1 & 0xFFFF),
                (unsigned long long)e.rip,
                (unsigned long long)e.rax,
                (unsigned long long)e.rcx,
                (unsigned long long)e.info2);
    }
    else
    {
        fprintf(fp,
                "tsc=%016llx cpu=%u code=0x%04x rip=%016llx "
                "info1=%016llx info2=%016llx rax=%016llx rcx=%016llx\n",
                (unsigned long long)e.tsc,
                (unsigned)e.cpu,
                (unsigned)e.code,
                (unsigned long long)e.rip,
                (unsigned long long)e.info1,
                (unsigned long long)e.info2,
                (unsigned long long)e.rax,
                (unsigned long long)e.rcx);
    }
}

int cmd_vmexit_dump(int argc, LPWSTR* argv)
{
    if (argc < 3)
    {
        fwprintf(stderr, L"usage: mvm-ctrl.exe vmexit-dump <path>\n");
        return 1;
    }

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, argv[2], L"w") != 0 || !fp)
    {
        fwprintf(stderr, L"vmexit-dump: failed to open %s\n", argv[2]);
        return 1;
    }

    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    constexpr uint32_t kBatch  = 64;
    void*              raw_buf = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void*              raw_hnd = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!raw_buf || !raw_hnd)
    {
        fprintf(stderr, "vmexit-dump: VirtualAlloc failed\n");
        fclose(fp);
        return 1;
    }
    auto* buf    = static_cast<HvVmexitLogEntry*>(raw_buf);
    auto* handle = static_cast<uint64_t*>(raw_hnd);

    VirtualLock(raw_buf, 4096);
    VirtualLock(raw_hnd, 4096);
    RtlZeroMemory(buf, 4096);
    RtlZeroMemory(handle, 4096);
    handle[0]      = 0;
    handle[1]      = 0;
    uint64_t total = 0, total_dropped = 0;

    fprintf(fp, "=== vmexit-dump ===\n");

    for (;;)
    {
        uint64_t result = 0;
        if (!mvm::do_vmmcall(VMMCALL_HV_DRAIN_VMEXIT_LOG,
                                  reinterpret_cast<uint64_t>(buf),
                                  kBatch,
                                  reinterpret_cast<uint64_t>(handle),
                                  &result))
        {
            fprintf(stderr, "vmexit-dump: VMMCALL failed -- HV absent?\n");
            break;
        }
        const uint32_t n = static_cast<uint32_t>(result);
        if (handle[1] != 0)
        {
            fprintf(fp, "[dropped %llu entries]\n", (unsigned long long)handle[1]);
            total_dropped += handle[1];
            handle[1]      = 0;
        }
        for (uint32_t i = 0; i < n; ++i)
            write_entry(fp, buf[i]);
        total += n;
        if (n < kBatch)
            break;
    }

    fprintf(
        fp, "=== end (%llu entries, %llu dropped) ===\n", (unsigned long long)total, (unsigned long long)total_dropped);
    fclose(fp);
    VirtualFree(raw_buf, 0, MEM_RELEASE);
    VirtualFree(raw_hnd, 0, MEM_RELEASE);
    printf("vmexit-dump: %llu entries written to %ls (%llu dropped)\n",
           (unsigned long long)total,
           argv[2],
           (unsigned long long)total_dropped);
    return 0;
}
