#include "mvm_client.h"

extern "C" uint64_t svm_client_vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t key);

namespace mvm
{

bool do_vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result)
{
    if (!out_result)
    {
        return false;
    }

    __try
    {
        *out_result = svm_client_vmmcall(reason, p1, p2, p3, VMMCALL_PRESHARED_KEY);
        return true;
    }
    __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ? EXCEPTION_EXECUTE_HANDLER
                                                                  : EXCEPTION_CONTINUE_SEARCH)
    {
        return false;
    }
}

bool hypervisor_present()
{
    uint64_t result = 0;
    return do_vmmcall(VMMCALL_HYPERVISOR_PRESENT, 0, 0, 0, &result);
}

bool request_shutdown()
{
    uint64_t result = 0;
    return do_vmmcall(VMMCALL_REQUEST_SHUTDOWN, 0, 0, 0, &result) && result == 1;
}

static void force_global_vmexit()
{
    DWORD_PTR process_affinity, system_affinity;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity))
    {
        HANDLE thread = GetCurrentThread();
        for (int i = 0; i < sizeof(DWORD_PTR) * 8; ++i)
        {
            if (system_affinity & (1ULL << i))
            {
                DWORD_PTR prev = SetThreadAffinityMask(thread, (1ULL << i));
                if (prev)
                {
                    uint64_t dummy = 0;
                    do_vmmcall(VMMCALL_HYPERVISOR_PRESENT, 0, 0, 0, &dummy);
                    SetThreadAffinityMask(thread, prev);
                }
            }
        }
    }
}

uint64_t microvm_create(uint64_t size_in_bytes)
{
    uint64_t handle = 0;
    if (do_vmmcall(VMMCALL_MICROVM_CREATE, size_in_bytes, 0, 0, &handle))
    {
        return handle;
    }
    return 0;
}

bool microvm_destroy(uint64_t handle)
{
    uint64_t result = 0;
    bool     ok     = do_vmmcall(VMMCALL_MICROVM_DESTROY, handle, 0, 0, &result) && result == 1;
    force_global_vmexit();
    Sleep(1);
    return ok;
}

bool microvm_map_mailbox(uint64_t handle, uint64_t mailbox_va)
{
    uint64_t result = 0;
    return do_vmmcall(VMMCALL_MICROVM_MAP_MAILBOX, handle, mailbox_va, 0, &result) && result == 1;
}

bool microvm_load(
    uint64_t handle, const void* code, uint64_t code_size, uint64_t stack_size, uint64_t flags, uint8_t xor_key)
{
    if (!code || code_size == 0 || xor_key == 0)
    {
        return false;
    }

    const bool     pre_encrypted = (flags & MICROVM_FLAG_PRE_ENCRYPTED) != 0;
    const uint64_t hv_flags      = flags & ~MICROVM_FLAG_PRE_ENCRYPTED;

    uint8_t stub[] = { 0x48, 0x8d, 0x35, 0x0e, 0x00, 0x00, 0x00, 0xb9, 0x00, 0x00, 0x00,
                       0x00, 0xb2, 0x00, 0x30, 0x16, 0x48, 0xff, 0xc6, 0xe2, 0xf9 };

    uint8_t* owned_buffer = nullptr;
    uint64_t load_size    = 0;

    if (pre_encrypted)
    {
        owned_buffer = new uint8_t[code_size];
        memcpy(owned_buffer, code, code_size);
        load_size = code_size;
    }
    else
    {
        uint32_t payload_size32                = static_cast<uint32_t>(code_size);
        *reinterpret_cast<uint32_t*>(&stub[8]) = payload_size32;

        owned_buffer = new uint8_t[sizeof(stub) + code_size];
        memcpy(owned_buffer, stub, sizeof(stub));

        const uint8_t* original_code = static_cast<const uint8_t*>(code);
        for (uint64_t i = 0; i < code_size; ++i)
        {
            owned_buffer[sizeof(stub) + i] = original_code[i] ^ xor_key;
        }
        load_size = sizeof(stub) + code_size;
    }

    mvm_microvm_load_t request = {};
    request.handle                 = handle;
    request.code_va                = reinterpret_cast<uint64_t>(owned_buffer);
    request.code_size              = load_size;
    request.stack_size             = stack_size;
    request.flags                  = hv_flags;
    request.xor_key                = xor_key;

    uint64_t result = 0;
    bool     ok = do_vmmcall(VMMCALL_MICROVM_LOAD, reinterpret_cast<uint64_t>(&request), 0, 0, &result) && result == 1;

    delete[] owned_buffer;
    if (ok)
    {
        force_global_vmexit();
    }
    return ok;
}

int64_t microvm_step(uint64_t handle)
{
    uint64_t result = 0;
    if (!do_vmmcall(VMMCALL_STEP_MICROVM, handle, 0, 0, &result))
    {
        return -1;
    }

    return static_cast<int64_t>(result);
}

int64_t microvm_run(uint64_t handle, const void* code, uint64_t code_size, uint64_t stack_size, uint64_t max_steps)
{
    if (!microvm_load(handle, code, code_size, stack_size))
    {
        return -1;
    }

    for (uint64_t i = 0; i < max_steps; ++i)
    {
        int64_t r = microvm_step(handle);
        if (r < 0)
        {
            return r;
        }
        if (r == 1)
        {
            return 1;
        }
    }
    return 0;
}

bool client::present() const
{
    return hypervisor_present();
}

bool client::vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result)
{
    return do_vmmcall(reason, p1, p2, p3, out_result);
}

uint64_t client::microvm_create(uint64_t size_in_bytes)
{
    return mvm::microvm_create(size_in_bytes);
}

bool client::microvm_destroy(uint64_t handle)
{
    return mvm::microvm_destroy(handle);
}

bool client::microvm_load(
    uint64_t handle, const void* code, uint64_t code_size, uint64_t stack_size, uint64_t flags, uint8_t xor_key)
{
    return mvm::microvm_load(handle, code, code_size, stack_size, flags, xor_key);
}

int64_t client::microvm_step(uint64_t handle)
{
    return mvm::microvm_step(handle);
}

int64_t client::microvm_run(
    uint64_t handle, const void* code, uint64_t code_size, uint64_t stack_size, uint64_t max_steps)
{
    return mvm::microvm_run(handle, code, code_size, stack_size, max_steps);
}

}
