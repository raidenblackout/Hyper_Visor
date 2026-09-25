

#pragma once
#include "common.h"
#include "svm/vmcb.h"
#include "../../mvm-payload/include/microvm_config.h"

namespace micro_vm
{

constexpr uint32_t max_instances            = 16;
constexpr uint64_t default_private_ram_size = 0x400000;
constexpr uint64_t default_stack_size       = 0x10000;

constexpr uint32_t micro_vm_asid         = 128;
constexpr uint64_t VMMCALL_MICROVM_DONE  = 0xD0EEF00DDEADCAFEull;
constexpr uint64_t VMMCALL_MICROVM_YIELD = 0xD0EEF00DDEAD0002ull;
constexpr uint64_t IDENTITY_SIZE         = 0x10000000000ULL;
constexpr uint64_t PHYSMAP_BASE_HV       = 0x10000000000ULL;
constexpr uint64_t PRIVATE_RAM_BASE_GVA  = 0x20000000000ULL;
constexpr uint32_t saved_gpr_count       = 14;

struct instance_t
{
    uint64_t handle;
    bool     in_use;
    uint64_t private_ram_pa;
    uint64_t private_ram_size;
    uint64_t code_pa;
    uint64_t code_size;
    uint64_t stack_pa;
    uint64_t stack_size;
    uint64_t stack_top;
    uint64_t pml4_pa;
    uint64_t pdpt_identity_pa[2];
    uint64_t pdpt_physmap_pa[2];
    uint64_t pdpt_private_pa;
    uint64_t npt_root_pa;
    VMCB*    vmcb;
    uint64_t vmcb_pa;
    uint64_t config_gva;
    uint32_t mailbox_count;
    uint64_t mailbox_gvas[MICROVM_MAX_MAILBOXES];

    uint64_t saved_gprs[saved_gpr_count];

    alignas(16) uint8_t saved_fp[512];

    void* private_va;
    void* vmcb_va;
    void* npt_pml4_va;
    void* mbox_pd_va;
    void* mbox_pt_va;

    bool tlb_flush_pending;

    int64_t terminal_result;
    uint64_t last_exit_code, last_exit_rip, last_exit_info1, last_exit_info2;

    uint64_t n_steps;
    uint64_t n_exit_intr;
    uint64_t n_exit_vintr;
    uint64_t n_exit_vmmcall_done;
    uint64_t n_exit_vmmcall_yield;
    uint64_t n_exit_other;
};

struct system_t
{
    instance_t    instances[max_instances];
    volatile long alloc_lock;
};

bool system_init();

instance_t* create(uint64_t size);
bool        destroy(instance_t*);
instance_t* find(uint64_t handle);

bool load(instance_t*, const void* code, uint64_t code_size, uint64_t stack_size);

bool load_from_guest(
    instance_t*, VMCB* caller_vmcb, uint64_t code_va, uint64_t code_size, uint64_t stack_size, uint8_t xor_key);

bool guest_va_to_hpa(uint64_t guest_cr3, uint64_t va, uint64_t* out_pa);

bool map_mailbox(instance_t*, uint64_t mailbox_pa);

int64_t step(instance_t*);

}

extern "C" uint64_t AsmVmRunMicroVm(uint64_t vmcb_pa, uint64_t* saved_gprs, void* saved_fp);
