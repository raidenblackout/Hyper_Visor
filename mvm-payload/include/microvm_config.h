#pragma once
#include <cstdint>

constexpr uint64_t PHYSMAP_BASE          = 0x10000000000ULL;
constexpr uint32_t MICROVM_MAX_MAILBOXES = 16;
constexpr uint64_t MICROVM_VMMCALL_DONE  = 0xD0EEF00DDEADCAFEull;
constexpr uint64_t MICROVM_VMMCALL_YIELD = 0xD0EEF00DDEAD0002ull;

constexpr uint64_t PAGE_SIZE_4K = 0x1000;
constexpr uint64_t PAGE_MASK_4K = 0xFFF;
constexpr uint64_t PFN_MASK     = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t PTE_PRESENT  = 1ULL << 0;
constexpr uint64_t PTE_LARGE    = 1ULL << 7;

struct MicroVmConfig
{
    uint64_t magic;
    uint64_t physmap_base;

    uint64_t heap_base;
    uint64_t heap_size;

    uint32_t mailbox_count;
    uint32_t _pad0;
    uint64_t mailbox_gvas[MICROVM_MAX_MAILBOXES];

    uint32_t eprocess_dtb;
    uint32_t eprocess_pid;
    uint32_t eprocess_links;
    uint32_t eprocess_name;
    uint32_t eprocess_peb;
    uint32_t eprocess_name_length;

    uint64_t eprocess_list_head_va;
    uint64_t kernel_cr3;
};

constexpr uint64_t MICROVM_CONFIG_MAGIC = 0x434D564D;
