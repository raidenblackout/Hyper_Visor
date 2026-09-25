

#pragma once
#include <cstdint>

constexpr uint64_t VMMCALL_PRESHARED_KEY = 0x7A8B9C0D1E2F3A4Bull;

enum : uint64_t
{
    MICROVM_FLAG_PHYSMAP_WRITABLE = 1 << 0,
};

struct mvm_microvm_load_t
{
    uint64_t handle;
    uint64_t code_va;
    uint64_t code_size;
    uint64_t stack_size;
    uint64_t flags;
    uint8_t  xor_key;
    uint8_t  _pad[7];
};

constexpr uint64_t mvm_microvm_done_signal = 0xD0EEF00DDEADCAFEull;
