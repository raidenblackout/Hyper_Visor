#include "commands.h"
#include "mvm_client.h"
#include <cstdio>
#include <cwchar>

extern "C" const uint8_t microvm_check_yield, microvm_check_yield_end;
extern "C" const uint8_t microvm_check_fault, microvm_check_fault_end;
extern "C" const uint8_t microvm_check_done, microvm_check_done_end;
extern "C" const uint8_t microvm_check_spin, microvm_check_spin_end;

namespace {
void stage(const char* text)
{
    std::printf("microvm-check: %s\n", text);
    std::fflush(stdout);
}

bool load(uint64_t handle, const uint8_t* begin, const uint8_t* end)
{
    stage("loading payload");
    return mvm::microvm_load(handle, begin,
        reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(begin), 4096);
}

int64_t step(uint64_t handle)
{
    stage("entering step");
    const auto result = mvm::microvm_step(handle);
    std::printf("microvm-check: step returned %lld\n", static_cast<long long>(result));
    std::fflush(stdout);
    return result;
}

int64_t run(uint64_t handle)
{

    for (unsigned i = 0; i < 64; ++i)
    {
        const auto result = step(handle);
        if (result != 0)
            return result;
    }
    stage("FAIL: 64-step budget exhausted");
    return 0;
}

int run_intr_probe(uint64_t handle)
{
    if (!load(handle, &microvm_check_spin, &microvm_check_spin_end))
    {
        stage("FAIL: spin payload load failed");
        return 1;
    }

    constexpr unsigned step_budget = 128;
    unsigned steps_seen = 0;
    for (; steps_seen < step_budget; ++steps_seen)
    {
        const int64_t r = mvm::microvm_step(handle);
        if (r != 0)
        {
            std::printf("microvm-check: FAIL: step %u returned %lld (expected 0 = VMEXIT_INTR/VINTR)\n",
                        steps_seen, static_cast<long long>(r));
            std::fflush(stdout);
            return 1;
        }
    }
    std::printf("microvm-check: completed %u spin steps, all returned 0\n", steps_seen);
    std::fflush(stdout);

    uint64_t stats[6]     = {};
    uint64_t stats_result = 0;
    if (!mvm::do_vmmcall(VMMCALL_MICROVM_GET_STATS, handle,
                         reinterpret_cast<uint64_t>(stats), 0, &stats_result) ||
        stats_result != 1)
    {
        stage("FAIL: GET_STATS did not return");
        return 1;
    }
    std::printf("microvm-check: exit stats: steps=%llu intr=%llu vintr=%llu done=%llu yield=%llu other=%llu\n",
                (unsigned long long)stats[0], (unsigned long long)stats[1],
                (unsigned long long)stats[2], (unsigned long long)stats[3],
                (unsigned long long)stats[4], (unsigned long long)stats[5]);
    std::fflush(stdout);

    const uint64_t intr_total = stats[1] + stats[2];
    if (intr_total == 0)
    {
        stage("FAIL: no VMEXIT_INTR/VINTR observed - the intercept did not fire");
        return 1;
    }
    if (stats[3] != 0 || stats[5] != 0)
    {
        stage("FAIL: unexpected done/other exits from a pure spin loop");
        return 1;
    }
    std::printf("microvm-check: PASS - physical INTR intercept promoted %llu host interrupts to VMEXIT\n",
                (unsigned long long)intr_total);
    std::fflush(stdout);
    return 0;
}
}

int cmd_microvm_check(int argc, LPWSTR* argv)
{
    const bool is_yield = argc == 3 && std::wcscmp(argv[2], L"yield") == 0;
    const bool is_fault = argc == 3 && std::wcscmp(argv[2], L"fault") == 0;
    const bool is_intr  = argc == 3 && std::wcscmp(argv[2], L"intr")  == 0;
    if (!is_yield && !is_fault && !is_intr)
    {
        stage("usage: mvm-ctrl microvm-check yield|fault|intr");
        return 2;
    }
    if (is_intr)
        stage("intr check (spin loop; verifies host IRQs exit the MicroVM)");
    else if (is_fault)
        stage("fault check (deliberate UD2 inside guest)");
    else
        stage("yield check (three yields and SIMD verification)");
    stage("checking hypervisor presence");
    if (!mvm::hypervisor_present())
    {
        stage("FAIL: hypervisor is not present");
        return 1;
    }
    stage("creating VM");
    const uint64_t handle = mvm::microvm_create(4096);
    if (!handle)
    {
        stage("FAIL: create returned no handle");
        return 1;
    }

    int result = 1;
    if (is_intr)
    {
        result = run_intr_probe(handle);
    }
    else
    {
        bool ok = load(handle, is_fault ? &microvm_check_fault : &microvm_check_yield,
                              is_fault ? &microvm_check_fault_end : &microvm_check_yield_end);
        if (!ok)
            stage("FAIL: payload load failed");
        if (ok)
        {
            const int64_t expected = is_fault ? -1 : 1;
            ok = run(handle) == expected;
            if (ok)
            {
                stage("checking terminal status remains unchanged");
                ok = step(handle) == expected;
            }
        }
        if (ok && is_fault)
        {
            stage("reloading a completion payload into the same VM");
            ok = load(handle, &microvm_check_done, &microvm_check_done_end);
            if (ok)
                ok = run(handle) == 1 && step(handle) == 1;
        }
        stage(ok ? "PASS" : "FAIL: unexpected result (see last stage)");
        result = ok ? 0 : 1;
    }

    stage("destroying VM");
    const bool destroyed = mvm::microvm_destroy(handle);
    if (!destroyed && result == 0)
    {
        stage("FAIL: destroy failed after otherwise-passing run");
        result = 1;
    }
    return result;
}
