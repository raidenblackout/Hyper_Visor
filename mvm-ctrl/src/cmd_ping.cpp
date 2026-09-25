

#include "commands.h"
#include "device.h"
#include "mvm_client.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace
{

bool pin_and_confirm(uint32_t cpu)
{
    const DWORD_PTR mask = (DWORD_PTR)1 << cpu;
    const DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), mask);
    if (prev == 0)
    {
        printf("ping: SetThreadAffinityMask(cpu=%u) failed err=%lu\n", cpu, GetLastError());
        return false;
    }

    SwitchToThread();

    const DWORD landed = GetCurrentProcessorNumber();
    if (landed != cpu)
    {
        printf("ping: cpu=%u pin requested, landed on cpu=%lu\n", cpu, landed);
        return false;
    }
    return true;
}

int do_ping_burst(uint32_t cpu, uint32_t count)
{
    if (!pin_and_confirm(cpu))
        return 1;

    uint32_t ok = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (mvm::hypervisor_present())
            ++ok;
    }
    printf("ping: cpu=%u sent=%u answered=%u\n", cpu, count, ok);
    return (ok == count) ? 0 : 2;
}

}

int cmd_ping(int argc, LPWSTR* argv)
{
    uint32_t cpu   = 0;
    uint32_t count = 1;
    if (argc >= 3)
        cpu = (uint32_t)_wtoi(argv[2]);
    if (argc >= 4)
        count = (uint32_t)_wtoi(argv[3]);
    if (count == 0)
        count = 1;
    return do_ping_burst(cpu, count);
}

int cmd_ping_all(int argc, LPWSTR* argv)
{
    uint32_t count = 40;
    if (argc >= 3)
        count = (uint32_t)_wtoi(argv[2]);
    if (count == 0)
        count = 40;

    const DWORD ncpu = (DWORD)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    printf("ping-all: %lu processors, %u VMMCALLs each\n", ncpu, count);

    int worst = 0;
    for (uint32_t c = 0; c < ncpu && c < 64; ++c)
    {
        const int rc = do_ping_burst(c, count);
        if (rc > worst)
            worst = rc;
    }
    return worst;
}
