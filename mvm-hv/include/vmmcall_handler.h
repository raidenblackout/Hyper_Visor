

#pragma once
#include <cstdint>
#include "common.h"
#include "mvm_shared.h"

namespace vmm
{

enum : uint64_t
{
    VMMCALL_HYPERVISOR_PRESENT = 1,

    VMMCALL_REQUEST_SHUTDOWN    = 19,
    VMMCALL_MICROVM_LOAD        = 24,
    VMMCALL_STEP_MICROVM        = 25,
    VMMCALL_MICROVM_CREATE      = 26,
    VMMCALL_MICROVM_DESTROY     = 27,
    VMMCALL_MICROVM_MAP_MAILBOX = 28,

    VMMCALL_MICROVM_GET_STATS = 29,

    VMMCALL_HV_DRAIN_VMEXIT_LOG = 30,

    VMMCALL_NETLOG_CONTROL = 31,

    VMMCALL_FEATURES_CONTROL = 32,
};

enum : uint64_t
{
    HV_FEATURE_ID_FB_LOG  = 1,
    HV_FEATURE_ID_LOG_MEM = 2,
    HV_FEATURE_ID_NETLOG  = 3,
};

void dispatch_vmmcall(HOST_CONTEXT* ctx);

}
