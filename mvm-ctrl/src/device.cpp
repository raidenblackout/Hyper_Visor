#include "device.h"
#include <cstdio>

bool check_hypervisor_present()
{
    if (!mvm::hypervisor_present())
    {
        printf("Hypervisor did not answer - mvm-hv is not mapped\n"
               "(or was already unloaded).\n");
        return false;
    }

    printf("Hypervisor is loaded and running.\n");
    return true;
}

bool do_vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result)
{
    if (!mvm::do_vmmcall(reason, p1, p2, p3, out_result))
    {
        printf("VMMCALL 0x%llX went unanswered - hypervisor absent or already torn down\n",
               static_cast<unsigned long long>(reason));
        return false;
    }

    return true;
}
