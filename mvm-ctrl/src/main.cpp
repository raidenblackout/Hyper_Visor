#include "commands.h"
#include "device.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace
{

struct CommandEntry
{
    const wchar_t* name;
    int (*fn)(int argc, LPWSTR* argv);
    bool           needs_hv;
    const wchar_t* one_line_help;
};

constexpr CommandEntry kCommands[] = {
    { L"ping", cmd_ping, false, L"send N VMMCALLs from cpu (default cpu=0 count=1)" },
    { L"ping-all", cmd_ping_all, false, L"ping every logical processor (default count=40)" },
    { L"features", cmd_features, true, L"get/set runtime + boot-time HV toggles" },
    { L"netlog", cmd_netlog, true, L"turn UDP net-log on/off/status" },
    { L"vmexit-dump", cmd_vmexit_dump, false, L"drain the HV VMEXIT ring buffer to a file" },
    { L"microvm", cmd_microvm, false, L"run a payload inside a fresh micro-VM" },
    { L"microvm-check", cmd_microvm_check, false, L"check yield/SIMD, deliberate fault, or physical-INTR intercept: yield|fault|intr" },
    { L"microvm-test", cmd_microvm_test, false, L"run the bundled micro-VM test payload" },
    { L"microvm-bench", cmd_microvm_bench, false, L"run the bundled micro-VM bench payload" },
    { L"unload", cmd_unload, false, L"ask the HV to tear itself down" },
};

void print_usage()
{
    wprintf(L"mvm-ctrl -- control CLI for the mvm hypervisor\n"
            L"\n"
            L"usage: mvm-ctrl <command> [args...]\n"
            L"       mvm-ctrl                     # default: report HV presence\n"
            L"\n"
            L"commands:\n");
    for (const auto& c : kCommands)
        wprintf(L"  %-14s %s\n", c.name, c.one_line_help);
    wprintf(L"\n"
            L"Run a command with no args (where applicable) to see its own usage.\n");
}

int run_default_diagnostic()
{
    return check_hypervisor_present() ? 0 : 1;
}

}

int main()
{
    int     argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;

    int rc = 0;

    if (argc < 2)
    {
        rc = run_default_diagnostic();
    }
    else if (wcscmp(argv[1], L"-h") == 0 || wcscmp(argv[1], L"--help") == 0 ||
             wcscmp(argv[1], L"help") == 0 || wcscmp(argv[1], L"/?") == 0)
    {
        print_usage();
        rc = 0;
    }
    else
    {
        const CommandEntry* hit = nullptr;
        for (const auto& c : kCommands)
        {
            if (wcscmp(argv[1], c.name) == 0)
            {
                hit = &c;
                break;
            }
        }
        if (!hit)
        {
            fwprintf(stderr, L"mvm-ctrl: unknown command '%s'\n\n", argv[1]);
            print_usage();
            rc = 2;
        }
        else if (hit->needs_hv && !check_hypervisor_present())
        {
            rc = 1;
        }
        else
        {
            rc = hit->fn(argc, argv);
        }
    }

    LocalFree(argv);
    return rc;
}
