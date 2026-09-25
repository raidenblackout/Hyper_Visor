#pragma once
#include <windows.h>
#include <cstdint>
#include "mvm_shared.h"

enum : uint64_t
{
    VMMCALL_HYPERVISOR_PRESENT  = 1,
    VMMCALL_REQUEST_SHUTDOWN    = 19,
    VMMCALL_MICROVM_LOAD        = 24,
    VMMCALL_STEP_MICROVM        = 25,
    VMMCALL_MICROVM_CREATE      = 26,
    VMMCALL_MICROVM_DESTROY     = 27,
    VMMCALL_MICROVM_MAP_MAILBOX = 28,
    VMMCALL_MICROVM_GET_STATS   = 29,
    VMMCALL_HV_DRAIN_VMEXIT_LOG = 30,
    VMMCALL_NETLOG_CONTROL      = 31,
    VMMCALL_FEATURES_CONTROL    = 32,
};

enum
{
    HV_FEATURE_ID_FB_LOG  = 1,
    HV_FEATURE_ID_LOG_MEM = 2,
    HV_FEATURE_ID_NETLOG  = 3,
};

struct HvVmexitLogEntry
{
    uint64_t tsc;
    uint32_t cpu;
    uint32_t code;
    uint64_t rip;
    uint64_t info1;
    uint64_t info2;
    uint64_t rax;
    uint64_t rcx;
    uint64_t seq;
};

namespace mvm
{

bool do_vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result);

bool hypervisor_present();

bool request_shutdown();

uint64_t microvm_create(uint64_t size_in_bytes);
bool     microvm_destroy(uint64_t handle);
bool     microvm_map_mailbox(uint64_t handle, uint64_t mailbox_pa);

constexpr uint64_t MICROVM_FLAG_PRE_ENCRYPTED = 1ULL << 63;
bool               microvm_load(uint64_t    handle,
                                const void* code,
                                uint64_t    code_size,
                                uint64_t    stack_size = 0,
                                uint64_t    flags      = 0,
                                uint8_t     xor_key    = 0x7A);

int64_t microvm_step(uint64_t handle);

int64_t microvm_run(
    uint64_t handle, const void* code, uint64_t code_size, uint64_t stack_size = 0, uint64_t max_steps = 1000000);

class client
{
  public:
    client() = default;

    client(const client&)            = delete;
    client& operator=(const client&) = delete;

    bool present() const;

    uint64_t microvm_create(uint64_t size_in_bytes);
    bool     microvm_destroy(uint64_t handle);
    bool     microvm_load(uint64_t    handle,
                          const void* code,
                          uint64_t    code_size,
                          uint64_t    stack_size = 0,
                          uint64_t    flags      = 0,
                          uint8_t     xor_key    = 0x7A);
    int64_t  microvm_step(uint64_t handle);
    int64_t  microvm_run(
        uint64_t handle, const void* code, uint64_t code_size, uint64_t stack_size = 0, uint64_t max_steps = 1000000);

    bool vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result);
};
}
