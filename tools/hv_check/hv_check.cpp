

#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>

static const char* kExpectedVendor = "mvm         ";

struct CpuResult
{
    DWORD requested;
    DWORD actual;
    bool  migrated;
    char  vendor[13];
    bool  hvPresentBit;
    bool  svmBit;
};

static void ReadCpuidOnCurrentCpu(CpuResult& r)
{
    int regs[4] = {};

    __cpuid(regs, 0x40000000);
    std::memcpy(r.vendor + 0, &regs[1], 4);
    std::memcpy(r.vendor + 4, &regs[2], 4);
    std::memcpy(r.vendor + 8, &regs[3], 4);
    r.vendor[12] = '\0';

    __cpuid(regs, 1);
    r.hvPresentBit = (static_cast<unsigned>(regs[2]) & (1u << 31)) != 0;

    __cpuid(regs, 0x80000001);
    r.svmBit = (static_cast<unsigned>(regs[2]) & (1u << 2)) != 0;

    r.actual = GetCurrentProcessorNumber();
}

int main()
{
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    DWORD count = si.dwNumberOfProcessors;

    if (count > 64)
    {
        std::printf("NOTE: %lu processors reported; only the first 64 (group 0) are tested.\n\n", count);
        count = 64;
    }

    CpuResult* results = new CpuResult[count]();

    for (DWORD cpu = 0; cpu < count; ++cpu)
    {
        CpuResult& r = results[cpu];
        r.requested  = cpu;

        const DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << cpu);
        if (prev == 0)
        {
            r.migrated = false;
            std::snprintf(r.vendor, sizeof(r.vendor), "<affinity>");
            continue;
        }

        Sleep(0);
        ReadCpuidOnCurrentCpu(r);
        r.migrated = (r.actual == cpu);
    }

    SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(-1));

    std::printf("mvm presence check -- expecting vendor \"%s\"\n\n", kExpectedVendor);
    std::printf("  CPU  ran-on  vendor         hv-bit  svm-bit  verdict\n");
    std::printf("  ---  ------  -------------  ------  -------  -------\n");

    DWORD virtualized = 0, escaped = 0, unreliable = 0;

    for (DWORD cpu = 0; cpu < count; ++cpu)
    {
        const CpuResult& r = results[cpu];

        const char* verdict;
        if (!r.migrated)
        {
            verdict = "UNRELIABLE (did not run on target cpu)";
            ++unreliable;
        }
        else if (std::strcmp(r.vendor, kExpectedVendor) == 0)
        {
            if (!r.hvPresentBit)
                verdict = "HV (but CPUID.1 hv-bit clear!)";
            else if (r.svmBit)
                verdict = "HV (but SVM bit still set!)";
            else
                verdict = "virtualized";
            ++virtualized;
        }
        else
        {
            verdict = "ESCAPED -- no hypervisor on this cpu";
            ++escaped;
        }

        std::printf("  %3lu  %6lu  %-13s  %-6s  %-7s  %s\n",
                    r.requested,
                    r.actual,
                    r.vendor,
                    r.hvPresentBit ? "set" : "clear",
                    r.svmBit ? "SET" : "clear",
                    verdict);
    }

    std::printf("\n  %lu of %lu virtualized", virtualized, count);
    if (escaped)
        std::printf(", %lu ESCAPED", escaped);
    if (unreliable)
        std::printf(", %lu unreliable", unreliable);
    std::printf("\n\n");

    if (escaped == 0 && unreliable == 0 && virtualized == count)
        std::printf("  RESULT: hypervisor is live on every logical processor.\n");
    else if (virtualized > 0)
        std::printf("  RESULT: PARTIAL -- some processors are not virtualized.\n"
                    "          The BSP surviving while APs escape is the expected\n"
                    "          signature of INIT resetting an AP out of guest mode.\n");
    else
        std::printf("  RESULT: hypervisor is NOT present on any processor.\n");

    delete[] results;
    return 0;
}
