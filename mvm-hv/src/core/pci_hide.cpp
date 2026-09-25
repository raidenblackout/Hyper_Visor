
#include <intrin.h>

#include "net/pci_hide.h"
#include "svm/paging.h"
#include "boot_stubs.h"
#include "fb_panic.h"
#include "net/net_log.h"
#include "svm/vmcb.h"
#include "micro_vm.h"
#include "hv_terminate.h"

namespace pci_hide
{

static uint64_t g_regions[MAX_HIDDEN_REGIONS] = {};
static uint32_t g_region_count                = 0;
static stats    g_stats                       = {};

bool add_region(uint64_t gpa)
{
    gpa &= ~0xFFFULL;
    for (uint32_t i = 0; i < g_region_count; ++i)
        if (g_regions[i] == gpa)
            return true;
    if (g_region_count >= MAX_HIDDEN_REGIONS)
        return false;
    g_regions[g_region_count++] = gpa;
    return true;
}

bool is_hidden(uint64_t gpa)
{
    gpa &= ~0xFFFULL;
    for (uint32_t i = 0; i < g_region_count; ++i)
        if (g_regions[i] == gpa)
            return true;
    return false;
}

stats get_stats()
{
    return g_stats;
}

extern "C" void split_2mb_pde(PD_ENTRY_2MB* pde, PT_ENTRY_4KB* pt);

static bool split_and_hide(PAGING_STRUCTURES* pml4, uint64_t gpa, PT_ENTRY_4KB* pt_scratch)
{
    const uint32_t pml4_i = (gpa >> 39) & 0x1FF;
    const uint32_t pdpt_i = (gpa >> 30) & 0x1FF;
    const uint32_t pd_i   = (gpa >> 21) & 0x1FF;
    const uint32_t pt_i   = (gpa >> 12) & 0x1FF;

    if (pml4_i >= PAGING_PML4_COUNT)
        return false;

    auto& pde = pml4->Pd[pml4_i][pdpt_i][pd_i];

    PT_ENTRY_4KB* pt;
    if (pde.Bits.LargePage)
    {
        split_2mb_pde(&pde, pt_scratch);
        pt = pt_scratch;
    }
    else
    {
        pt = reinterpret_cast<PT_ENTRY_4KB*>(pde.Uint64 & 0x000FFFFFFFFFF000ULL);
    }

    pt[pt_i].Bits.Valid = 0;
    return true;
}

bool install_carveouts(ROOT_CONTEXT* root)
{
    if (g_region_count == 0)
        return true;
    for (uint32_t r = 0; r < g_region_count; ++r)
    {
        if (!split_and_hide(&root->Svm.NestedPageTables, g_regions[r], root->Svm.HiddenPt[0][r]))
            return false;
        if (!split_and_hide(&root->Svm.NestedPageTablesForBsp, g_regions[r], root->Svm.HiddenPt[1][r]))
            return false;
    }
    boot_stubs::log("pci_hide: installed %u carve-outs on both NPT roots", (unsigned)g_region_count);
    return true;
}

namespace
{

struct decoded_mov
{
    bool     is_write;
    bool     sign_extend;
    uint32_t width;
    uint32_t dest_reg_idx;
    uint32_t insn_len;
    uint64_t src_value;
};

static uint64_t* reg_slot(GUEST_REGISTERS* gr, uint32_t reg_idx)
{
    static constexpr int kMap[16] = {
        14, 13, 12, 11, -1, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    };
    if (reg_idx >= 16 || kMap[reg_idx] < 0)
        return nullptr;
    return reinterpret_cast<uint64_t*>(gr) + kMap[reg_idx];
}

static bool decode_mov(const uint8_t* bytes, uint32_t max_len, decoded_mov* out)
{
    uint32_t i         = 0;
    bool     prefix_66 = false;
    uint8_t  rex       = 0;
    while (i < max_len)
    {
        const uint8_t b = bytes[i];
        if (b == 0x66)
        {
            prefix_66 = true;
            ++i;
            continue;
        }
        else if ((b & 0xF0) == 0x40)
        {
            rex = b;
            ++i;
            continue;
        }
        else if (b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65 || b == 0xF0 || b == 0xF2 ||
                 b == 0xF3)
        {
            ++i;
            continue;
        }
        break;
    }
    if (i >= max_len)
        return false;

    uint8_t  op          = bytes[i++];
    bool     is_write    = false;
    bool     sign_extend = false;
    uint32_t width       = 4;
    switch (op)
    {
    case 0x88:
        is_write = true;
        width    = 1;
        break;
    case 0x8A:
        is_write = false;
        width    = 1;
        break;
    case 0x89:
        is_write = true;
        width    = prefix_66 ? 2 : (rex & 0x08 ? 8 : 4);
        break;
    case 0x8B:
        is_write = false;
        width    = prefix_66 ? 2 : (rex & 0x08 ? 8 : 4);
        break;
    case 0x0F:
        if (i >= max_len)
            return false;
        op = bytes[i++];
        switch (op)
        {
        case 0xB6:
            is_write = false;
            width    = 1;
            break;
        case 0xB7:
            is_write = false;
            width    = 2;
            break;
        case 0xBE:
            is_write    = false;
            width       = 1;
            sign_extend = true;
            break;
        case 0xBF:
            is_write    = false;
            width       = 2;
            sign_extend = true;
            break;
        default:
            return false;
        }
        break;
    default:
        return false;
    }
    if (i >= max_len)
        return false;
    uint8_t  modrm = bytes[i++];
    uint32_t mod   = modrm >> 6;
    uint32_t reg   = (modrm >> 3) & 7;
    uint32_t rm    = modrm & 7;
    if (rex & 0x04)
        reg |= 8;
    if (rex & 0x01)
        rm |= 8;

    if (mod == 3)
        return false;

    if ((rm & 7) == 4)
    {
        if (i >= max_len)
            return false;
        ++i;
    }

    if (mod == 1)
        i += 1;
    else if (mod == 2)
        i += 4;
    else if (mod == 0 && (rm & 7) == 5)
        i += 4;
    if (i > max_len)
        return false;

    out->is_write     = is_write;
    out->width        = width;
    out->sign_extend  = sign_extend;
    out->dest_reg_idx = reg;
    out->insn_len     = i;
    return true;
}

}

void emulate(HOST_CONTEXT* ctx, uint64_t fault_gpa)
{
    auto& vmcb = ctx->Cpu->GuestVmcb;
    auto* gr   = ctx->Cpu->States.Guest.Registers;

    const uint8_t* bytes      = nullptr;
    uint32_t       byte_count = 0;
    uint8_t        fetched[16];

    if (vmcb.ControlArea.GuestInstructionBytes[0] != 0 || vmcb.ControlArea.NumOfBytesFetched != 0)
    {
        bytes      = vmcb.ControlArea.GuestInstructionBytes;
        byte_count = vmcb.ControlArea.NumOfBytesFetched;
    }
    else
    {
        uint64_t rip = vmcb.StateSaveArea.Rip;
        uint64_t hpa = 0;
        if (micro_vm::guest_va_to_hpa(vmcb.StateSaveArea.Cr3, rip, &hpa))
        {
            for (uint32_t k = 0; k < 16; ++k)
                fetched[k] = reinterpret_cast<uint8_t*>(hpa)[k];
            bytes      = fetched;
            byte_count = 16;
        }
    }

    decoded_mov d = {};
    if (!bytes || !decode_mov(bytes, byte_count, &d))
    {
        ++g_stats.decode_failures;
        const uint64_t rip = vmcb.StateSaveArea.Rip;
        fb_panic::reportf("pci_hide: UNDECODABLE INSN on hidden gpa=0x%llx rip=0x%llx -- HALT",
                          (unsigned long long)fault_gpa,
                          (unsigned long long)rip);
        if (bytes && byte_count)
        {
            fb_panic::reportf("  bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                              bytes[0],
                              byte_count > 1 ? bytes[1] : 0,
                              byte_count > 2 ? bytes[2] : 0,
                              byte_count > 3 ? bytes[3] : 0,
                              byte_count > 4 ? bytes[4] : 0,
                              byte_count > 5 ? bytes[5] : 0,
                              byte_count > 6 ? bytes[6] : 0,
                              byte_count > 7 ? bytes[7] : 0);
        }
        else
        {
            fb_panic::report("  (no instruction bytes available)");
        }
        hv_terminate_record(HALT_PCI_HIDE_FAIL);
        net_log::flush();
        _disable();
        for (;;)
        {
            __halt();
        }
    }

    if (d.is_write)
    {
    }
    else
    {
        uint64_t* dst = reg_slot(gr, d.dest_reg_idx);
        if (dst)
        {
            uint64_t v = (d.width >= 4) ? 0xFFFFFFFFULL : (d.width == 2) ? 0xFFFFULL : 0xFFULL;
            if (d.sign_extend)
                v = 0xFFFFFFFFFFFFFFFFULL;
            *dst = v;
        }
    }

    ++g_stats.emulated;
    SvmAdvanceRip(&vmcb, d.insn_len);
    (void)fault_gpa;
}

}
