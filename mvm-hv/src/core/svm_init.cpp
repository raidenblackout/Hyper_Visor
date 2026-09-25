

#include <ntddk.h>
#include <intrin.h>

#include "common.h"
#include "svm/cpuid.h"
#include "svm/msr.h"
#include "asm/vm_intrin.h"
#include "boot_stubs.h"
#include "hvmem/hvmem.h"
#include "svm_probe.h"
#include "npt_setup.h"
#include "host_state.h"
#include "fb_panic.h"
#include "net/pci_hide.h"
#include "net/pci.h"
#include "net/rtl8125.h"
#include "../../bootloader/net/netlog_cfg.h"

extern "C" void AsmDefaultExceptionHandlers(void);
extern "C" void AsmLaunchVm(void* HostRsp);

extern "C" volatile uint32_t g_virt_entry[64] = {};
extern "C" volatile uint32_t g_virt_skip[64]  = {};

extern "C" uintptr_t AsmReadInstructionPointer(void);
extern "C" uintptr_t AsmReadStackPointer(void);

extern "C" PT_ENTRY_4KB*     setup_bsp_lapic_pte(ROOT_CONTEXT* root);
extern "C" void              split_2mb_pde(PD_ENTRY_2MB* pde, PT_ENTRY_4KB* pt);
extern "C" void              arm_bsp_lapic_intercept_now(PER_CPU_DATA* cpu);
extern "C" volatile uint32_t g_remaining_sipi_count;
extern "C" long              g_x2apic_wrmsr_budget;

static PT_ENTRY_4KB* BspLocalApicNestedPte = nullptr;

namespace
{
union SegmentAttribute
{
    struct
    {
        uint16_t Type        : 4;
        uint16_t System      : 1;
        uint16_t Dpl         : 2;
        uint16_t Present     : 1;
        uint16_t Avl         : 1;
        uint16_t LongMode    : 1;
        uint16_t DefaultBit  : 1;
        uint16_t Granularity : 1;
        uint16_t Reserved1   : 4;
    } Bits;
    uint16_t Uint16;
};
static_assert(sizeof(SegmentAttribute) == 2, "SegmentAttribute size");

constexpr uint16_t kRplMask = 3;

uint16_t GetSegmentAccessRight(uint16_t selector, uint64_t gdtBase)
{
    auto* desc = reinterpret_cast<IA32_SEGMENT_DESCRIPTOR*>(gdtBase + (selector & ~kRplMask));

    SegmentAttribute attr = {};
    attr.Bits.Type        = static_cast<uint16_t>(desc->Bits.Type);
    attr.Bits.System      = static_cast<uint16_t>(desc->Bits.S);
    attr.Bits.Dpl         = static_cast<uint16_t>(desc->Bits.DPL);
    attr.Bits.Present     = static_cast<uint16_t>(desc->Bits.P);
    attr.Bits.Avl         = static_cast<uint16_t>(desc->Bits.AVL);
    attr.Bits.LongMode    = static_cast<uint16_t>(desc->Bits.L);
    attr.Bits.DefaultBit  = static_cast<uint16_t>(desc->Bits.DB);
    attr.Bits.Granularity = static_cast<uint16_t>(desc->Bits.G);
    return attr.Uint16;
}
}

struct GUEST_CONTEXT
{
    IA32_DESCRIPTOR Gdtr;
    IA32_DESCRIPTOR Idtr;
    uint16_t        SegCs;
    uint16_t        SegDs;
    uint16_t        SegEs;
    uint16_t        SegSs;
    uint64_t        Efer;
    uint64_t        GPat;
    uint64_t        Cr0;
    uint64_t        Cr2;
    uint64_t        Cr3;
    uint64_t        Cr4;
    uint64_t        Rflags;
    uint64_t        Rsp;
    uint64_t        Rip;
};

#define CAPTURE_CONTEXT(cr)                                               \
    do                                                                    \
    {                                                                     \
        svm_sgdt(reinterpret_cast<svm_descriptor_table_t*>(&(cr)->Gdtr)); \
        svm_sidt(reinterpret_cast<svm_descriptor_table_t*>(&(cr)->Idtr)); \
        (cr)->SegCs  = svm_read_cs();                                     \
        (cr)->SegDs  = svm_read_ds();                                     \
        (cr)->SegEs  = svm_read_es();                                     \
        (cr)->SegSs  = svm_read_ss();                                     \
        (cr)->Efer   = __readmsr(MSR_IA32_EFER);                          \
        (cr)->GPat   = __readmsr(MSR_IA32_PAT);                           \
        (cr)->Cr0    = __readcr0();                                       \
        (cr)->Cr2    = __readcr2();                                       \
        (cr)->Cr3    = __readcr3();                                       \
        (cr)->Cr4    = __readcr4();                                       \
        (cr)->Rflags = __readeflags();                                    \
        (cr)->Rsp    = AsmReadStackPointer();                             \
        (cr)->Rip    = AsmReadInstructionPointer();                       \
    } while (0)

static_assert(sizeof(svm_descriptor_table_t) == sizeof(IA32_DESCRIPTOR),
              "svm_descriptor_table_t must match IA32_DESCRIPTOR layout");

static void PrepareForVmrun(ROOT_CONTEXT* root, uint32_t procNum, const GUEST_CONTEXT* ctx)
{
    PER_CPU_DATA* cpu = &root->Cpus[procNum];

    const bool trace = (procNum == 0);

    cpu->GuestVmcb.ControlArea.InterceptException = g_intercept_exception;
    cpu->GuestVmcb.ControlArea.InterceptMisc1     = g_intercept_misc1;
    cpu->GuestVmcb.ControlArea.InterceptMisc2     = g_intercept_misc2;
    (void)procNum;
    cpu->GuestVmcb.ControlArea.MsrpmBasePa = reinterpret_cast<uint64_t>(&root->Svm.MsrPermissionsBitmap[0]);

    cpu->GuestVmcb.ControlArea.PauseFilterCount     = 4096;
    cpu->GuestVmcb.ControlArea.PauseFilterThreshold = 4096;

    if (trace)
        boot_stubs::log("PrepareForVmrun: intercepts excp=0x%x m1=0x%x m2=0x%x (cur VM_CR=0x%llx)",
                        (unsigned)cpu->GuestVmcb.ControlArea.InterceptException,
                        (unsigned)cpu->GuestVmcb.ControlArea.InterceptMisc1,
                        (unsigned)cpu->GuestVmcb.ControlArea.InterceptMisc2,
                        (unsigned long long)__readmsr(SVM_MSR_VM_CR));

#if HV_FEATURE_INIT_REDIRECT

    __writemsr(SVM_MSR_VM_CR, __readmsr(SVM_MSR_VM_CR) | VM_CR_R_INIT);

    if (trace)
        boot_stubs::log("PrepareForVmrun: VM_CR written (now=0x%llx)", (unsigned long long)__readmsr(SVM_MSR_VM_CR));
#else

    if (trace)
        boot_stubs::log("PrepareForVmrun: INIT redirect DISABLED (bisect)");
#endif

    if (g_hv_svm_max_asid != 0 && (procNum + 1) >= g_hv_svm_max_asid)
    {
        boot_stubs::log("svm_init: PrepareForVmrun cpu%u -- ASID %u exceeds CPUID NASID %u (halting)",
                        procNum,
                        procNum + 1,
                        g_hv_svm_max_asid);
        boot_stubs::halt("svm_init: ASID exceeds CPUID NASID limit");
    }
    cpu->GuestVmcb.ControlArea.GuestAsid = procNum + 1;

    if (g_hv_svm_eraps_capable)
    {
        cpu->GuestVmcb.ControlArea.TlbControl |= HV_SVM_TLB_CTRL_CLEAR_RAP;
        if (trace)
            boot_stubs::log("PrepareForVmrun: ERAPS CLEAR_RAP armed on cpu%u", procNum);
    }

#if HV_FEATURE_LBR

    {
        int svmRegs[4] = {};
        __cpuid(svmRegs, 0x8000000A);
        const bool lbrVirt = (static_cast<uint32_t>(svmRegs[3]) & (1u << 1)) != 0;

        if (lbrVirt)
        {
            cpu->GuestVmcb.ControlArea.LbrVirtualizationEnable  = 1;
            cpu->GuestVmcb.StateSaveArea.DbgCtl                |= 1;
        }

        if (trace)
            boot_stubs::log("PrepareForVmrun: LBR virt %s (cpuid 8000000A edx=0x%x)",
                            lbrVirt ? "ENABLED" : "UNSUPPORTED -- br_from will read 0",
                            (unsigned)svmRegs[3]);
    }
#endif

    cpu->GuestVmcb.ControlArea.NpEnable = SVM_NP_ENABLE_NP_ENABLE;
#if HV_FEATURE_LAPIC_INTERCEPT
    cpu->GuestVmcb.ControlArea.NCr3 = (procNum == 0)
                                          ? reinterpret_cast<uint64_t>(&root->Svm.NestedPageTablesForBsp.Pml4[0])
                                          : reinterpret_cast<uint64_t>(&root->Svm.NestedPageTables.Pml4[0]);
#else

    cpu->GuestVmcb.ControlArea.NCr3 = reinterpret_cast<uint64_t>(&root->Svm.NestedPageTables.Pml4[0]);
#endif

    auto& s     = cpu->GuestVmcb.StateSaveArea;
    s.GdtrBase  = ctx->Gdtr.Base;
    s.GdtrLimit = ctx->Gdtr.Limit;
    s.IdtrBase  = ctx->Idtr.Base;
    s.IdtrLimit = ctx->Idtr.Limit;

    s.CsLimit    = __segmentlimit(ctx->SegCs);
    s.DsLimit    = __segmentlimit(ctx->SegDs);
    s.EsLimit    = __segmentlimit(ctx->SegEs);
    s.SsLimit    = __segmentlimit(ctx->SegSs);
    s.CsSelector = ctx->SegCs;
    s.DsSelector = ctx->SegDs;
    s.EsSelector = ctx->SegEs;
    s.SsSelector = ctx->SegSs;
    s.CsAttrib   = GetSegmentAccessRight(ctx->SegCs, ctx->Gdtr.Base);
    s.DsAttrib   = GetSegmentAccessRight(ctx->SegDs, ctx->Gdtr.Base);
    s.EsAttrib   = GetSegmentAccessRight(ctx->SegEs, ctx->Gdtr.Base);
    s.SsAttrib   = GetSegmentAccessRight(ctx->SegSs, ctx->Gdtr.Base);

    s.Efer   = ctx->Efer;
    s.GPat   = ctx->GPat;
    s.Cr0    = ctx->Cr0;
    s.Cr2    = ctx->Cr2;
    s.Cr3    = ctx->Cr3;
    s.Cr4    = ctx->Cr4;
    s.Rflags = ctx->Rflags;
    s.Rsp    = ctx->Rsp;
    s.Rip    = ctx->Rip;

    if (trace)
        boot_stubs::log("PrepareForVmrun: state save filled, VMSAVE guest next (vmcb=%p)", &cpu->GuestVmcb);

    __svm_vmsave(reinterpret_cast<size_t>(&cpu->GuestVmcb));

    if (trace)
        boot_stubs::log("PrepareForVmrun: VMSAVE guest done");

    {
        uint64_t*      slot = reinterpret_cast<uint64_t*>(&cpu->HostStack.Raw[0]);
        const uint32_t n    = (HOST_STACK_SIZE - sizeof(HOST_STACK_BASED_PARAMETERS)) / sizeof(uint64_t);
        for (uint32_t i = 0; i < n; ++i)
            slot[i] = HOST_STACK_CANARY;
    }

    cpu->HostStack.Layout.Params.GuestVmcbPa     = reinterpret_cast<uint64_t>(&cpu->GuestVmcb);
    cpu->HostStack.Layout.Params.SharedContext   = root;
    cpu->HostStack.Layout.Params.ProcessorNumber = procNum;
    cpu->HostStack.Layout.Params.Reserved1       = HOST_STACK_PARAMS_SENTINEL;

    __writemsr(SVM_MSR_VM_HSAVE_PA, reinterpret_cast<uint64_t>(&cpu->HostStateArea[0]));
    if (trace)
        boot_stubs::log("PrepareForVmrun: VM_HSAVE_PA written (0x%p)", &cpu->HostStateArea[0]);

    int cpuidRegs[4] = {};
    __cpuid(cpuidRegs, 1);
    cpu->States.Guest.ProcessorNumber = procNum;
    cpu->States.Guest.ApicId          = static_cast<uint32_t>(cpuidRegs[1]) >> 24;
    cpu->States.Guest.ActivityState   = GuestStateActive;
    cpu->States.Guest.ApicAccessState = ApicAccessPassthrough;
    cpu->States.Svm.LocalApicNestedPte =
#if HV_FEATURE_LAPIC_INTERCEPT
        (procNum == 0) ? BspLocalApicNestedPte : nullptr;
#else
        nullptr;
#endif

    arm_bsp_lapic_intercept_now(cpu);
    if (trace && procNum == 0)
        boot_stubs::log("PrepareForVmrun: BSP LAPIC intercept armed eagerly (pre-VMRUN)");

    if (trace)
        boot_stubs::log("PrepareForVmrun: state bookkeeping done, InitHostData next");

    InitializeHostData(&cpu->HostX64Data, cpu->HostStack.Layout.Params.SharedContext ? &root->HostX64 : nullptr);
    if (trace)
        boot_stubs::log("PrepareForVmrun: InitHostData done, SwitchToHostContext next (host_cr3=0x%llx "
                        "host_idt_base=0x%llx host_gdt_base=0x%llx host_gdt_limit=0x%x)",
                        (unsigned long long)root->HostX64.Cr3,
                        (unsigned long long)root->HostX64.Idtr.Base,
                        (unsigned long long)cpu->HostX64Data.Gdtr.Base,
                        (unsigned)cpu->HostX64Data.Gdtr.Limit);

    SwitchToHostContext(&root->HostX64, &cpu->HostX64Data);
    if (trace)
        boot_stubs::log("PrepareForVmrun: SwitchToHostContext done, VMSAVE host next (vmcb=%p)", &cpu->HostVmcb);

    __svm_vmsave(reinterpret_cast<size_t>(&cpu->HostVmcb));

    if (trace)
        boot_stubs::log("PrepareForVmrun: VMSAVE host done");
}

extern "C" void VirtualizeProcessor(ROOT_CONTEXT* root, uint32_t procNum)
{
    GUEST_CONTEXT guestCtx = {};

    if (procNum < 64)
        g_virt_entry[procNum]++;

    if (root->Cpus[procNum].States.Guest.Virtualized)
    {
        if (procNum < 64)
            g_virt_skip[procNum]++;
        boot_stubs::log("svm_init: VirtualizeProcessor(cpu%u) SKIPPED -- already virtualized (re-entry)", procNum);
        return;
    }

    __writemsr(MSR_IA32_EFER, __readmsr(MSR_IA32_EFER) | EFER_SVME);

    boot_stubs::set_firmware_sinks(false);

    svm_clgi();

    CAPTURE_CONTEXT(&guestCtx);

    if (!root->Cpus[procNum].States.Guest.Virtualized)
    {
        boot_stubs::log("svm_init: virtualizing processor %u (VMCB@%p) captured_rip=0x%llx captured_rsp=0x%llx "
                        "captured_efer=0x%llx captured_cr0=0x%llx captured_cr3=0x%llx captured_cr4=0x%llx "
                        "captured_rflags=0x%llx IF=%u",
                        procNum,
                        &root->Cpus[procNum].GuestVmcb,
                        (unsigned long long)guestCtx.Rip,
                        (unsigned long long)guestCtx.Rsp,
                        (unsigned long long)guestCtx.Efer,
                        (unsigned long long)guestCtx.Cr0,
                        (unsigned long long)guestCtx.Cr3,
                        (unsigned long long)guestCtx.Cr4,
                        (unsigned long long)guestCtx.Rflags,
                        (unsigned)((guestCtx.Rflags >> 9) & 1));

        PrepareForVmrun(root, procNum, &guestCtx);
        boot_stubs::log("svm_init: PrepareForVmrun done, calling AsmLaunchVm on cpu%u", procNum);

        root->Cpus[procNum].States.Guest.Virtualized = 1;

        AsmLaunchVm(&root->Cpus[procNum].HostStack.Layout.Params);
        boot_stubs::halt("svm_init: AsmLaunchVm returned -- unreachable");
    }

    boot_stubs::set_firmware_sinks(true);

    boot_stubs::log("svm_init: processor %u now running as guest under mvm", procNum);
}

extern netlog_cfg_v4* g_netlog_cfg;

extern "C" bool InitializeSharedContext(ROOT_CONTEXT* root, uint32_t procCount, uint16_t hostCs)
{
    if (!IsSystemCompatible())
    {
        return false;
    }

    __stosb((unsigned char*)root, 0, root_context_bytes(procCount));

    InitializeHostSharedData(&root->HostX64, procCount, hostCs);
    SetupIdentityMapping(&root->Svm.NestedPageTables, true);
    SetupIdentityMapping(&root->Svm.NestedPageTablesForBsp, true);

    fb_panic::markf(fb_panic::kSlotBarHide, "BAR-HIDE: ENTERING");
    if (g_netlog_cfg && g_netlog_cfg->enable &&
        (g_netlog_cfg->mode == NETLOG_MODE_POST_ONLY || g_netlog_cfg->mode == NETLOG_MODE_BOTH))
    {
        if (g_netlog_cfg->mcfg_base_pa && g_netlog_cfg->hv_bdf)
        {
            const uint64_t ecam = g_netlog_cfg->mcfg_base_pa + ((uint64_t)((g_netlog_cfg->hv_bdf >> 8) & 0xFF) << 20) +
                                  ((uint64_t)((g_netlog_cfg->hv_bdf >> 3) & 0x1F) << 15) +
                                  ((uint64_t)(g_netlog_cfg->hv_bdf & 0x07) << 12);
            pci_hide::add_region(ecam);
            boot_stubs::log("pci_hide: registered ECAM carve-out at 0x%llx", (unsigned long long)ecam);

            pci::device dev = {};
            if (pci::probe(g_netlog_cfg->mcfg_base_pa, g_netlog_cfg->hv_bdf, &dev) &&
                dev.vendor_id == rtl8125::VENDOR_REALTEK && dev.bar[2] != 0)
            {
                const uint64_t size      = dev.bar_size[2] ? dev.bar_size[2] : 0x10000ULL;
                const uint64_t cap       = size > 0x10000ULL ? 0x10000ULL : size;
                uint32_t       bar_pages = 0;
                for (uint64_t off = 0; off < cap; off += 0x1000)
                {
                    if (pci_hide::add_region(dev.bar[2] + off))
                        ++bar_pages;
                    else
                        break;
                }
                boot_stubs::log("pci_hide: registered %u BAR2 pages (BAR2=0x%llx cap=0x%llx)",
                                (unsigned)bar_pages,
                                (unsigned long long)dev.bar[2],
                                (unsigned long long)cap);
            }
        }
        if (!pci_hide::install_carveouts(root))
        {
            boot_stubs::log("pci_hide: install_carveouts FAILED -- disabling netlog");
            g_netlog_cfg->enable = 0;
        }
    }
    fb_panic::markf(fb_panic::kSlotBarHide,
                    "BAR-HIDE: DONE (NETLOG_ENABLE=%u)",
                    (unsigned)(g_netlog_cfg ? g_netlog_cfg->enable : 0));

#if HV_FEATURE_HVB_NPT_PROTECT

    {
        const uint64_t rc_start = reinterpret_cast<uint64_t>(root);
        const uint64_t rc_end   = rc_start + root_context_bytes(procCount);
        ProtectRootContextInNpt(&root->Svm.NestedPageTables, rc_start, rc_end);
        ProtectRootContextInNpt(&root->Svm.NestedPageTablesForBsp, rc_start, rc_end);
        boot_stubs::log("svm_init: NPT-protected ROOT_CONTEXT 0x%llx..0x%llx (guest RO)",
                        (unsigned long long)rc_start,
                        (unsigned long long)rc_end);
    }
#endif

#if HV_FEATURE_HOST_WATCH

    ProtectHostStateAreas(root, procCount);
#endif

#if HV_FEATURE_LAPIC_INTERCEPT

    BspLocalApicNestedPte = setup_bsp_lapic_pte(root);
#else

    BspLocalApicNestedPte = nullptr;
#endif

#if HV_FEATURE_SVM_GUARD

    {
        constexpr uint32_t kOff                = 0xC0010117u - 0xC0010000u;
        constexpr uint32_t kByte               = 0x1000u + (kOff / 4);
        constexpr uint32_t kWriteBit           = (kOff & 3) * 2 + 1;
        root->Svm.MsrPermissionsBitmap[kByte] |= (1u << kWriteBit);
        boot_stubs::log("svm_init: SVM_GUARD armed (WRMSR VM_HSAVE_PA + VMLOAD/VMSAVE/STGI/CLGI, all CPUs)");
    }
#endif

    g_remaining_sipi_count = (procCount > 1) ? (procCount - 1) * 2 : 0;

    g_x2apic_wrmsr_budget = static_cast<long>(procCount) * 8;
    if (g_x2apic_wrmsr_budget < 32)
        g_x2apic_wrmsr_budget = 32;

    boot_stubs::log("svm_init: shared context built (root=%p host_cr3=0x%llx npt=%p bsp_npt=%p bsp_lapic_pte=%p "
                    "host_idt=%p sipi_count=%u wrmsr_budget=%d)",
                    root,
                    root->HostX64.Cr3,
                    &root->Svm.NestedPageTables.Pml4[0],
                    &root->Svm.NestedPageTablesForBsp.Pml4[0],
                    BspLocalApicNestedPte,
                    &root->HostX64.Idt[0],
                    (unsigned)g_remaining_sipi_count,
                    (int)g_x2apic_wrmsr_budget);
    return true;
}
