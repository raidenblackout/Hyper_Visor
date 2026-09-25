

#include "micro_vm.h"
#include "svm/vmcb.h"
#include "svm/msr.h"
#include "hvmem/hvmem.h"
#include <intrin.h>

namespace micro_vm
{

static system_t g_state = {};

namespace
{

constexpr uint64_t handle_salt = 0xC0FFEE12345678ABull;

constexpr uint64_t PTE_P     = 1ULL << 0;
constexpr uint64_t PTE_RW    = 1ULL << 1;
constexpr uint64_t PTE_US    = 1ULL << 2;
constexpr uint64_t PTE_PS    = 1ULL << 7;
constexpr uint64_t PTE_ATTRS = PTE_P | PTE_RW | PTE_US;

void lock_acquire()
{
    while (_InterlockedCompareExchange(&g_state.alloc_lock, 1, 0) != 0)
    {
        _mm_pause();
    }
}
void lock_release()
{
    _InterlockedExchange(&g_state.alloc_lock, 0);
}

void fill_pdpt_identity_1gb(uint64_t* pdpt_hva, uint64_t pa_base)
{
    for (uint32_t i = 0; i < 512; ++i)
    {
        const uint64_t pa = pa_base + (static_cast<uint64_t>(i) << 30);
        pdpt_hva[i]       = (pa & 0xFFFFFFFFC0000000ULL) | PTE_ATTRS | PTE_PS;
    }
}

void zero_page(uint64_t pa)
{
    __stosb(reinterpret_cast<uint8_t*>(pa), 0, HV_PAGE_SIZE);
}

template <typename T> T* as_ptr(uint64_t pa)
{
    return reinterpret_cast<T*>(pa);
}

}

bool system_init()
{
    for (uint32_t i = 0; i < max_instances; ++i)
    {
        g_state.instances[i] = {};
    }
    g_state.alloc_lock = 0;
    return true;
}

instance_t* create(uint64_t requested_size)
{
    lock_acquire();

    uint32_t slot = max_instances;
    for (uint32_t i = 0; i < max_instances; ++i)
    {
        if (!g_state.instances[i].in_use)
        {
            slot = i;
            break;
        }
    }
    if (slot == max_instances)
    {
        lock_release();
        return nullptr;
    }

    uint64_t private_size = requested_size < default_private_ram_size ? default_private_ram_size : requested_size;
    private_size          = (private_size + 0x1FFFFFULL) & ~0x1FFFFFULL;

    void* private_va = hvmem::allocate(private_size, HV_PAGE_SIZE);
    if (!private_va)
    {
        lock_release();
        return nullptr;
    }
    const uint64_t private_pa = hvmem::virt_to_phys(private_va);

    void* vmcb_va = hvmem::allocate(HV_PAGE_SIZE, HV_PAGE_SIZE);
    if (!vmcb_va)
    {
        hvmem::release(private_va, private_size);
        lock_release();
        return nullptr;
    }
    const uint64_t vmcb_pa = hvmem::virt_to_phys(vmcb_va);

    for (uint32_t i = 0; i < 6; ++i)
    {
        zero_page(private_pa + i * HV_PAGE_SIZE);
    }
    zero_page(vmcb_pa);

    instance_t& inst         = g_state.instances[slot];
    inst                     = {};
    inst.private_ram_pa      = private_pa;
    inst.private_ram_size    = private_size;
    inst.pml4_pa             = private_pa + 0x0000;
    inst.pdpt_identity_pa[0] = private_pa + 0x1000;
    inst.pdpt_identity_pa[1] = private_pa + 0x2000;
    inst.pdpt_physmap_pa[0]  = private_pa + 0x3000;
    inst.pdpt_physmap_pa[1]  = private_pa + 0x4000;
    inst.pdpt_private_pa     = private_pa + 0x5000;

    inst.code_pa    = private_pa + 0x6000;
    inst.stack_pa   = private_pa + private_size - default_stack_size;
    inst.stack_size = default_stack_size;
    inst.stack_top  = inst.stack_pa + inst.stack_size;
    inst.vmcb       = as_ptr<VMCB>(vmcb_pa);
    inst.vmcb_pa    = vmcb_pa;
    inst.private_va = private_va;
    inst.vmcb_va    = vmcb_va;

    uint64_t* pml4 = as_ptr<uint64_t>(inst.pml4_pa);
    pml4[0]        = inst.pdpt_identity_pa[0] | PTE_ATTRS;
    pml4[1]        = inst.pdpt_identity_pa[1] | PTE_ATTRS;

    pml4[2] = inst.pdpt_physmap_pa[0] | PTE_ATTRS;
    pml4[3] = inst.pdpt_physmap_pa[1] | PTE_ATTRS;

    pml4[4] = inst.pdpt_private_pa | PTE_ATTRS;

    fill_pdpt_identity_1gb(as_ptr<uint64_t>(inst.pdpt_identity_pa[0]), 0);
    fill_pdpt_identity_1gb(as_ptr<uint64_t>(inst.pdpt_identity_pa[1]), 0x8000000000ULL);

    fill_pdpt_identity_1gb(as_ptr<uint64_t>(inst.pdpt_physmap_pa[0]), 0);
    fill_pdpt_identity_1gb(as_ptr<uint64_t>(inst.pdpt_physmap_pa[1]), 0x8000000000ULL);

    void* mbox_pd_va = hvmem::allocate(HV_PAGE_SIZE, HV_PAGE_SIZE);
    void* mbox_pt_va = hvmem::allocate(HV_PAGE_SIZE, HV_PAGE_SIZE);
    if (!mbox_pd_va || !mbox_pt_va)
    {
        if (mbox_pd_va)
            hvmem::release(mbox_pd_va, HV_PAGE_SIZE);
        if (mbox_pt_va)
            hvmem::release(mbox_pt_va, HV_PAGE_SIZE);
        hvmem::release(vmcb_va, HV_PAGE_SIZE);
        hvmem::release(private_va, private_size);
        lock_release();
        return nullptr;
    }
    const uint64_t mbox_pd_pa = hvmem::virt_to_phys(mbox_pd_va);
    const uint64_t mbox_pt_pa = hvmem::virt_to_phys(mbox_pt_va);
    zero_page(mbox_pd_pa);
    zero_page(mbox_pt_pa);

    as_ptr<uint64_t>(inst.pdpt_private_pa)[0] = mbox_pd_pa | PTE_ATTRS;
    as_ptr<uint64_t>(mbox_pd_pa)[0]           = mbox_pt_pa | PTE_ATTRS;
    inst.mbox_pd_va                           = mbox_pd_va;
    inst.mbox_pt_va                           = mbox_pt_va;

    void* npt_pml4_va = hvmem::allocate(HV_PAGE_SIZE, HV_PAGE_SIZE);
    if (!npt_pml4_va)
    {
        hvmem::release(vmcb_va, HV_PAGE_SIZE);
        hvmem::release(private_va, private_size);
        lock_release();
        return nullptr;
    }
    const uint64_t npt_pml4_pa = hvmem::virt_to_phys(npt_pml4_va);
    zero_page(npt_pml4_pa);
    uint64_t* npt_pml4 = as_ptr<uint64_t>(npt_pml4_pa);
    npt_pml4[0]        = inst.pdpt_identity_pa[0] | PTE_ATTRS;
    npt_pml4[1]        = inst.pdpt_identity_pa[1] | PTE_ATTRS;

    inst.npt_root_pa = npt_pml4_pa;
    inst.npt_pml4_va = npt_pml4_va;

    auto& v = *inst.vmcb;

    v.ControlArea.InterceptException = 0xFFFFFFFFu;
    v.ControlArea.InterceptMisc1 = (1UL << 0) | SVM_INTERCEPT_MISC1_VINTR | SVM_INTERCEPT_MISC1_SHUTDOWN;
    v.ControlArea.InterceptMisc2 = SVM_INTERCEPT_MISC2_VMRUN | SVM_INTERCEPT_MISC2_VMMCALL;
    v.ControlArea.NCr3           = inst.npt_root_pa;
    v.ControlArea.NpEnable       = SVM_NP_ENABLE_NP_ENABLE;
    v.ControlArea.GuestAsid      = micro_vm_asid;

    v.ControlArea.TlbControl = 0;
    inst.tlb_flush_pending   = true;

    auto& s = v.StateSaveArea;
    s.Cr0   = 0x80000031ULL;
    s.Cr3   = inst.pml4_pa;
    s.Cr4   = 0x000006B0ULL;
    s.Efer  = (1ULL << 8) | (1ULL << 10) | (1ULL << 12);

    s.GPat = __readmsr(MSR_IA32_PAT);
    if (s.GPat == 0)
        s.GPat = 0x0007040600070406ULL;

    s.Rflags = 0x202;
    s.Rip    = inst.code_pa;
    s.Rsp    = inst.stack_top;

    s.CsSelector = 0x08;
    s.CsAttrib   = 0xA9B;
    s.CsBase     = 0;
    s.CsLimit    = 0xFFFFFFFF;
    s.DsSelector = 0x10;
    s.DsAttrib   = 0xC93;
    s.DsBase     = 0;
    s.DsLimit    = 0xFFFFFFFF;
    s.SsSelector = 0x10;
    s.SsAttrib   = 0xC93;
    s.SsBase     = 0;
    s.SsLimit    = 0xFFFFFFFF;
    s.Cpl        = 0;

    inst.handle = (reinterpret_cast<uint64_t>(&inst) ^ handle_salt);
    inst.in_use = true;

    lock_release();
    return &inst;
}

bool destroy(instance_t* inst)
{
    if (!inst || !inst->in_use)
        return false;

    lock_acquire();

    if (inst->mbox_pt_va)
        hvmem::release(inst->mbox_pt_va, HV_PAGE_SIZE);
    if (inst->mbox_pd_va)
        hvmem::release(inst->mbox_pd_va, HV_PAGE_SIZE);
    if (inst->npt_pml4_va)
        hvmem::release(inst->npt_pml4_va, HV_PAGE_SIZE);
    if (inst->vmcb_va)
        hvmem::release(inst->vmcb_va, HV_PAGE_SIZE);
    if (inst->private_va)
        hvmem::release(inst->private_va, inst->private_ram_size);

    *inst = {};

    lock_release();
    return true;
}

instance_t* find(uint64_t handle)
{
    if (handle == 0)
        return nullptr;
    for (uint32_t i = 0; i < max_instances; ++i)
    {
        if (g_state.instances[i].in_use && g_state.instances[i].handle == handle)
        {
            return &g_state.instances[i];
        }
    }
    return nullptr;
}

bool load(instance_t*, const void*, uint64_t, uint64_t)
{
    return false;
}

bool guest_va_to_hpa(uint64_t guest_cr3, uint64_t va, uint64_t* out_pa)
{
    const uint64_t pml4_pa = guest_cr3 & 0xFFFFFFFFFF000ULL;
    const uint64_t pml4_i  = (va >> 39) & 0x1FF;
    const uint64_t pdpt_i  = (va >> 30) & 0x1FF;
    const uint64_t pd_i    = (va >> 21) & 0x1FF;
    const uint64_t pt_i    = (va >> 12) & 0x1FF;

    const uint64_t pml4e = as_ptr<uint64_t>(pml4_pa)[pml4_i];
    if (!(pml4e & PTE_P))
        return false;

    const uint64_t pdpt_pa = pml4e & 0xFFFFFFFFFF000ULL;
    const uint64_t pdpte   = as_ptr<uint64_t>(pdpt_pa)[pdpt_i];
    if (!(pdpte & PTE_P))
        return false;
    if (pdpte & PTE_PS)
    {
        *out_pa = (pdpte & 0xFFFFC0000000ULL) | (va & 0x3FFFFFFFULL);
        return true;
    }

    const uint64_t pd_pa = pdpte & 0xFFFFFFFFFF000ULL;
    const uint64_t pde   = as_ptr<uint64_t>(pd_pa)[pd_i];
    if (!(pde & PTE_P))
        return false;
    if (pde & PTE_PS)
    {
        *out_pa = (pde & 0xFFFFFFE00000ULL) | (va & 0x1FFFFFULL);
        return true;
    }

    const uint64_t pt_pa = pde & 0xFFFFFFFFFF000ULL;
    const uint64_t pte   = as_ptr<uint64_t>(pt_pa)[pt_i];
    if (!(pte & PTE_P))
        return false;
    *out_pa = (pte & 0xFFFFFFFFF000ULL) | (va & 0xFFFULL);
    return true;
}

namespace
{

uint64_t read_u64_from_guest(uint64_t guest_cr3, uint64_t va)
{
    uint64_t pa = 0;
    if (!guest_va_to_hpa(guest_cr3, va, &pa))
        return 0;
    return *as_ptr<uint64_t>(pa);
}

constexpr uint32_t KPCR_CURRENT_THREAD   = 0x188;
constexpr uint32_t KTHREAD_PROCESS       = 0x0B8;
constexpr uint32_t EPROCESS_DTB          = 0x028;
constexpr uint32_t EPROCESS_UNIQUE_PID   = 0x1D0;
constexpr uint32_t EPROCESS_ACTIVE_LINKS = 0x1D8;
constexpr uint32_t EPROCESS_IMAGE_NAME   = 0x338;
constexpr uint32_t EPROCESS_PEB          = 0x2E0;
constexpr uint32_t EPROCESS_NAME_LENGTH  = 16;

void populate_kernel_info(MicroVmConfig* cfg, VMCB* guest_vmcb)
{
    const auto&    save    = guest_vmcb->StateSaveArea;
    const uint64_t kpcr_va = (save.Cpl == 0) ? save.GsBase : save.KernelGsBase;
    if (kpcr_va == 0)
        return;

    const uint64_t guest_cr3  = save.Cr3;
    const uint64_t kthread_va = read_u64_from_guest(guest_cr3, kpcr_va + KPCR_CURRENT_THREAD);
    if (kthread_va == 0)
        return;
    const uint64_t eprocess_va = read_u64_from_guest(guest_cr3, kthread_va + KTHREAD_PROCESS);
    if (eprocess_va == 0)
        return;
    const uint64_t dtb = read_u64_from_guest(guest_cr3, eprocess_va + EPROCESS_DTB);

    cfg->eprocess_dtb          = EPROCESS_DTB;
    cfg->eprocess_pid          = EPROCESS_UNIQUE_PID;
    cfg->eprocess_links        = EPROCESS_ACTIVE_LINKS;
    cfg->eprocess_name         = EPROCESS_IMAGE_NAME;
    cfg->eprocess_peb          = EPROCESS_PEB;
    cfg->eprocess_name_length  = EPROCESS_NAME_LENGTH;
    cfg->eprocess_list_head_va = eprocess_va;

    cfg->kernel_cr3 = dtb & 0xFFFFFFFFF000ULL;
}

constexpr uint64_t decrypt_stub_size                        = 21;
constexpr uint8_t  decrypt_stub_template[decrypt_stub_size] = {
    0x48, 0x8D, 0x35, 0x0E, 0x00, 0x00, 0x00, 0xB9, 0x00, 0x00, 0x00,
    0x00, 0xB2, 0x00, 0x30, 0x16, 0x48, 0xFF, 0xC6, 0xE2, 0xF9,
};

constexpr uint64_t decrypt_stub_size_off = 8;
constexpr uint64_t decrypt_stub_key_off  = 13;

bool payload_has_decrypt_stub(uint64_t code_pa, uint64_t code_size)
{
    if (code_size <= decrypt_stub_size)
        return false;

    const uint8_t* p = as_ptr<uint8_t>(code_pa);
    for (uint64_t i = 0; i < decrypt_stub_size; ++i)
    {
        if (i >= decrypt_stub_size_off && i < decrypt_stub_size_off + 4)
            continue;
        if (i == decrypt_stub_key_off)
            continue;
        if (p[i] != decrypt_stub_template[i])
            return false;
    }

    const uint32_t patched_size = *reinterpret_cast<const uint32_t*>(p + decrypt_stub_size_off);
    if (patched_size != (code_size - decrypt_stub_size))
        return false;

    return true;
}

bool copy_from_guest(uint64_t guest_cr3, uint64_t src_va, uint64_t dst_pa, uint64_t size)
{
    uint64_t bytes_left = size;
    while (bytes_left > 0)
    {
        uint64_t src_pa = 0;
        if (!guest_va_to_hpa(guest_cr3, src_va, &src_pa))
            return false;

        const uint64_t page_off = src_va & 0xFFFULL;
        const uint64_t chunk    = (0x1000ULL - page_off) < bytes_left ? (0x1000ULL - page_off) : bytes_left;

        __movsb(as_ptr<uint8_t>(dst_pa), as_ptr<uint8_t>(src_pa), chunk);

        src_va     += chunk;
        dst_pa     += chunk;
        bytes_left -= chunk;
    }
    return true;
}

}

bool load_from_guest(
    instance_t* inst, VMCB* caller_vmcb, uint64_t code_va, uint64_t code_size, uint64_t stack_size, uint8_t xor_key)
{
    if (!inst || !inst->in_use || code_size == 0 || !caller_vmcb)
        return false;

    if (xor_key == 0)
        return false;

    const uint64_t caller_cr3 = caller_vmcb->StateSaveArea.Cr3;

    if (stack_size == 0)
        stack_size = default_stack_size;
    stack_size = (stack_size + 0xFFFULL) & ~0xFFFULL;

    constexpr uint64_t config_size = HV_PAGE_SIZE;

    const uint64_t max_code = (inst->stack_top - stack_size - config_size) - inst->code_pa;
    if (code_size > max_code)
        return false;

    if (!copy_from_guest(caller_cr3, code_va, inst->code_pa, code_size))
        return false;

    if (!payload_has_decrypt_stub(inst->code_pa, code_size))
        return false;

    as_ptr<uint8_t>(inst->code_pa)[decrypt_stub_key_off] = xor_key;

    inst->code_size  = code_size;
    inst->stack_size = stack_size;

    const uint64_t config_pa = inst->stack_top - stack_size - config_size;
    inst->config_gva         = config_pa;
    auto* cfg                = as_ptr<MicroVmConfig>(config_pa);
    __stosb(reinterpret_cast<uint8_t*>(cfg), 0, sizeof(*cfg));
    cfg->magic         = MICROVM_CONFIG_MAGIC;
    cfg->physmap_base  = PHYSMAP_BASE_HV;
    cfg->heap_base     = inst->code_pa + code_size;
    cfg->heap_size     = config_pa - cfg->heap_base;
    cfg->mailbox_count = 0;

    populate_kernel_info(cfg, caller_vmcb);

    for (uint32_t i = 0; i < saved_gpr_count; ++i)
        inst->saved_gprs[i] = 0;
    inst->saved_gprs[4] = inst->config_gva;

    __stosb(inst->saved_fp, 0, sizeof(inst->saved_fp));
    *reinterpret_cast<uint16_t*>(inst->saved_fp) = 0x037F;
    *reinterpret_cast<uint32_t*>(inst->saved_fp + 24) = 0x1F80;

    auto& s = inst->vmcb->StateSaveArea;
    s.Rip   = inst->code_pa;

    s.Rsp = inst->stack_top;

    inst->terminal_result = 0;
    inst->last_exit_code = inst->last_exit_rip = inst->last_exit_info1 = inst->last_exit_info2 = 0;

    return true;
}

bool map_mailbox(instance_t* inst, uint64_t mailbox_pa)
{
    if (!inst || !inst->in_use)
        return false;
    if (inst->mailbox_count >= MICROVM_MAX_MAILBOXES)
        return false;
    if (mailbox_pa & 0xFFFULL)
        return false;

    const uint32_t slot = inst->mailbox_count;

    const uint64_t gva = PRIVATE_RAM_BASE_GVA + static_cast<uint64_t>(slot) * HV_PAGE_SIZE;

    const uint64_t mbox_pt_pa          = hvmem::virt_to_phys(inst->mbox_pt_va);
    as_ptr<uint64_t>(mbox_pt_pa)[slot] = (mailbox_pa & 0xFFFFFFFFF000ULL) | PTE_ATTRS;

    if (inst->config_gva != 0)
    {
        auto* cfg               = as_ptr<MicroVmConfig>(inst->config_gva);
        cfg->mailbox_gvas[slot] = gva;
        cfg->mailbox_count      = slot + 1;
    }

    inst->mailbox_gvas[slot] = gva;
    inst->mailbox_count      = slot + 1;

    inst->tlb_flush_pending = true;
    return true;
}

int64_t step(instance_t* inst)
{
    if (!inst || !inst->in_use)
        return -1;

    if (inst->terminal_result != 0)
        return inst->terminal_result;

    if (inst->tlb_flush_pending)
    {
        inst->vmcb->ControlArea.TlbControl = 3;
        inst->tlb_flush_pending            = false;
    }
    else
    {
        inst->vmcb->ControlArea.TlbControl = 0;
    }

    inst->vmcb->ControlArea.VIntr = 0;

    const uint64_t exit_code = AsmVmRunMicroVm(inst->vmcb_pa, inst->saved_gprs, inst->saved_fp);

    ++inst->n_steps;
    inst->last_exit_code = exit_code;
    inst->last_exit_rip = inst->vmcb->StateSaveArea.Rip;
    inst->last_exit_info1 = inst->vmcb->ControlArea.ExitInfo1;
    inst->last_exit_info2 = inst->vmcb->ControlArea.ExitInfo2;

    switch (exit_code)
    {
    case VMEXIT_VMMCALL:
    {
        const uint64_t rax = inst->vmcb->StateSaveArea.Rax;
        if (rax == VMMCALL_MICROVM_DONE)
        {
            ++inst->n_exit_vmmcall_done;
            return inst->terminal_result = 1;
        }
        if (rax == VMMCALL_MICROVM_YIELD)
        {
            ++inst->n_exit_vmmcall_yield;
            SvmAdvanceRip(inst->vmcb, 3);
            return 0;
        }
        ++inst->n_exit_other;
        return inst->terminal_result = -1;
    }

    case VMEXIT_INTR:
        ++inst->n_exit_intr;
        return 0;
    case VMEXIT_VINTR:
        ++inst->n_exit_vintr;
        return 0;
    case VMEXIT_SHUTDOWN:
        ++inst->n_exit_other;
        return inst->terminal_result = -2;
    default:
        ++inst->n_exit_other;
        return inst->terminal_result = -1;
    }
}

}
