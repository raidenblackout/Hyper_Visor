

#include "commands.h"
#include "device.h"
#include "mvm_client.h"

#include <cstdio>

int cmd_unload(int, LPWSTR*)
{
    uint64_t result = 0;
    if (!do_vmmcall(VMMCALL_REQUEST_SHUTDOWN, 0, 0, 0, &result))
    {
        printf("unload: VMMCALL_REQUEST_SHUTDOWN went unanswered - is the driver mapped?\n");
        return 1;
    }
    if (result != 1)
    {
        printf("unload: HV returned %llu (0 = placeholder handler, teardown not yet implemented)\n",
               static_cast<unsigned long long>(result));
        return 1;
    }
    printf("unload: HV teardown acknowledged\n");
    return 0;
}
