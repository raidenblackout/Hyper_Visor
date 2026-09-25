

#include <ntddk.h>
#include <intrin.h>

#include "boot_time.h"
#include "hv_platform.h"
#include "boot_stubs.h"
#include "fb_panic.h"
#include "../../bootloader/net/netlog_cfg.h"

extern "C" int  SnpSendInit(void* system_table, netlog_cfg_v4* cfg, void (*log_fn)(const char*));
extern "C" void SnpSendShutdown(void);
#include "hvmem/hvmem.h"
#include "common.h"
#include "asm/vm_intrin.h"
#include "svm/mp_services.h"
#include "micro_vm.h"
#include "vmexit_log.h"
#include "net/net_log.h"

#include "hvb.h"

extern "C" void HvPublishWatch(uint32_t slot, uint64_t pa);

extern "C" bool InitializeSharedContext(ROOT_CONTEXT* root, uint32_t procCount, uint16_t hostCs);
extern "C" void VirtualizeProcessor(ROOT_CONTEXT* root, uint32_t procNum);

static EFI_MP_SERVICES_PROTOCOL_t* g_mp          = nullptr;
static ROOT_CONTEXT*               g_root        = nullptr;
netlog_cfg_v4*                     g_netlog_cfg  = nullptr;
hv_features_v1*                    g_hv_features = nullptr;

extern "C" uint32_t g_intercept_misc1     = 0;
extern "C" uint32_t g_intercept_misc2     = 0;
extern "C" uint32_t g_intercept_exception = 0;

namespace
{
constexpr uint64_t kEfiSuccess = 0;
constexpr uint64_t kEfiError   = (1ULL << 63);
constexpr uint64_t kEfiAborted = kEfiError | 21;
}

static void EFIAPI virtualize_processor_ap(void*)
{
    EFI_UINTN_T  procNum = 0;
    EFI_STATUS_T s       = g_mp->WhoAmI(g_mp, &procNum);
    if (s != 0)
    {
        boot_stubs::halt("boot_time: WhoAmI failed on AP");
    }

    if (HV_MAX_VIRT_APS != 0 && static_cast<uint32_t>(procNum) > HV_MAX_VIRT_APS)
    {
        boot_stubs::log(
            "boot_time: AP %u left NATIVE (HV_MAX_VIRT_APS=%u cap)", (unsigned)procNum, (unsigned)HV_MAX_VIRT_APS);
        return;
    }

    VirtualizeProcessor(g_root, static_cast<uint32_t>(procNum));
}

#pragma comment(linker, "/EXPORT:hvruntime_boot_entry")

extern "C" __declspec(dllexport) uint64_t hvruntime_boot_entry(void* SystemTable, uint64_t hvb_pa)
{
    void* hvb = reinterpret_cast<void*>(static_cast<uintptr_t>(hvb_pa));

    hv_platform::enter_boot_time(hvb, SystemTable);

    auto* hdr = static_cast<hvb_header_t*>(hvb);
    if (hdr->magic != HVB_MAGIC || hdr->version != HVB_VERSION)
    {
        boot_stubs::log("hvruntime: HVB magic/version mismatch (magic=0x%x ver=%u)", hdr->magic, hdr->version);
        hv_platform::leave_boot_time();
        return kEfiAborted;
    }

    g_netlog_cfg  = &hdr->netlog;
    g_hv_features = &hdr->features;

    {
        uint32_t m1 = 0;
        if (hdr->features.intercept_pause)
            m1 |= SVM_INTERCEPT_MISC1_PAUSE;
        if (hdr->features.intercept_shutdown)
            m1 |= SVM_INTERCEPT_MISC1_SHUTDOWN;
        if (hdr->features.intercept_msr_prot)
            m1 |= SVM_INTERCEPT_MISC1_MSR_PROT;
        g_intercept_misc1 = m1;

        uint32_t m2 = SVM_INTERCEPT_MISC2_VMRUN | SVM_INTERCEPT_MISC2_VMMCALL;
        if (hdr->features.intercept_svm_guard)
            m2 |= SVM_INTERCEPT_MISC2_VMLOAD | SVM_INTERCEPT_MISC2_VMSAVE | SVM_INTERCEPT_MISC2_STGI |
                  SVM_INTERCEPT_MISC2_CLGI;
        g_intercept_misc2 = m2;

        g_intercept_exception = HV_INTERCEPT_EXCEPTION_FIXED;

        boot_stubs::log("hvruntime: intercepts m1=0x%x m2=0x%x excp=0x%x (from hv_features)",
                        g_intercept_misc1,
                        g_intercept_misc2,
                        g_intercept_exception);
    }

    (void)SnpSendInit(SystemTable, &hdr->netlog, [](const char* line) { boot_stubs::log("%s", line); });

    boot_stubs::log(
        "hvruntime: entry hvb_pa=0x%llx heap_off=0x%llx heap_sz=0x%llx", hvb_pa, hdr->heap_offset, hdr->heap_size);

    void*    heap_va = static_cast<uint8_t*>(hvb) + hdr->heap_offset;
    uint64_t heap_pa = hvb_pa + hdr->heap_offset;
    if (!hvmem::initialize(heap_va, heap_pa, hdr->heap_size))
    {
        boot_stubs::log("hvruntime: hvmem::initialize FAILED");
        hv_platform::leave_boot_time();
        return kEfiAborted;
    }
    boot_stubs::log("hvruntime: hvmem ready heap_va=%p heap_pa=0x%llx", heap_va, heap_pa);

    if (hdr->debug_log_file_ptr)
    {
        hv_platform::attach_debug_log(reinterpret_cast<void*>(static_cast<uintptr_t>(hdr->debug_log_file_ptr)));
        boot_stubs::log("hvruntime: live log attached (file=0x%llx)", (uint64_t)hdr->debug_log_file_ptr);
    }

    fb_panic::init(hvb);

    fb_panic::set_enabled(hdr->features.fb_log != 0);
    boot_stubs::set_log_mem_enabled(hdr->features.log_mem != 0);
    boot_stubs::log("hvruntime: %s (fb=0x%llx %ux%u stride=%u fb_log=%u log_mem=%u)",
                    fb_panic::status(),
                    (uint64_t)hdr->fb_base,
                    hdr->fb_width,
                    hdr->fb_height,
                    hdr->fb_stride,
                    (unsigned)hdr->features.fb_log,
                    (unsigned)hdr->features.log_mem);

    if (fb_panic::available())
    {
        fb_panic::reportf("HV FB LOG ARMED - FB=0x%llx %ux%u STRIDE=%u",
                          (uint64_t)hdr->fb_base,
                          hdr->fb_width,
                          hdr->fb_height,
                          hdr->fb_stride);
    }

    auto* st = static_cast<EFI_SYSTEM_TABLE_MIN_t*>(SystemTable);
    auto* bs = st->BootServices;

    HvPublishWatch(2, reinterpret_cast<uint64_t>(&st->BootServices));
    HvPublishWatch(3, reinterpret_cast<uint64_t>(&st->RuntimeServices));
    boot_stubs::log(
        "hvruntime: watch published gST=%p &BootSvc=%p &RunSvc=%p", st, &st->BootServices, &st->RuntimeServices);

#if HV_FEATURE_SBRUN_EBS_FIXUP

    {
        const uint64_t rtFromSt    = reinterpret_cast<uint64_t>(st->RuntimeServices);
        const uint64_t rtFromSbrun = *reinterpret_cast<volatile uint64_t*>(HV_SBRUN_GRT_PA);
        const uint64_t before      = *reinterpret_cast<volatile uint64_t*>(HV_SBRUN_CACHEDRT_PA);

        const uint64_t rt = (rtFromSbrun == rtFromSt && rtFromSbrun != 0) ? rtFromSbrun : rtFromSt;

        *reinterpret_cast<volatile uint64_t*>(HV_SBRUN_CACHEDRT_PA) = rt;
        *reinterpret_cast<volatile uint8_t*>(HV_SBRUN_INITFLAG_PA)  = 1;

        boot_stubs::log(
            "hvruntime: SBRun EBS fixup cachedRT 0x%llx->0x%llx (gRT_sbrun=0x%llx st->RT=0x%llx initFlag=1)",
            (unsigned long long)before,
            (unsigned long long)rt,
            (unsigned long long)rtFromSbrun,
            (unsigned long long)rtFromSt);
    }
#endif

    EFI_GUID_t   mpGuid = gEfiMpServiceProtocolGuid;
    EFI_STATUS_T s      = bs->LocateProtocol(&mpGuid, nullptr, (void**)&g_mp);
    if (s != 0 || g_mp == nullptr)
    {
        boot_stubs::log("hvruntime: LocateProtocol(MpServices) FAILED status=0x%llx", (unsigned long long)s);
        hv_platform::leave_boot_time();
        return kEfiAborted;
    }

    EFI_UINTN_T nProcessors = 0, nEnabled = 0;
    s = g_mp->GetNumberOfProcessors(g_mp, &nProcessors, &nEnabled);
    if (s != 0 || nEnabled == 0)
    {
        boot_stubs::log("hvruntime: GetNumberOfProcessors FAILED status=0x%llx n=%llu",
                        (unsigned long long)s,
                        (unsigned long long)nProcessors);
        hv_platform::leave_boot_time();
        return kEfiAborted;
    }
    boot_stubs::log("hvruntime: mp_services located, cpus total=%llu enabled=%llu",
                    (unsigned long long)nProcessors,
                    (unsigned long long)nEnabled);

    const uint32_t procCount = static_cast<uint32_t>(nEnabled);

    const uint64_t root_bytes = root_context_bytes(procCount);
    void*          root_va    = hvmem::allocate(root_bytes, hvmem::PAGE_BYTES);
    if (!root_va)
    {
        boot_stubs::log(
            "hvruntime: ROOT_CONTEXT alloc FAILED (needed 0x%llx bytes for %u cpus)", root_bytes, procCount);
        hv_platform::leave_boot_time();
        return kEfiAborted;
    }
    boot_stubs::log("hvruntime: ROOT_CONTEXT alloc %p bytes=0x%llx cpus=%u", root_va, root_bytes, procCount);

    g_root                 = static_cast<ROOT_CONTEXT*>(root_va);
    const uint16_t host_cs = svm_read_cs();

    if (!InitializeSharedContext(g_root, procCount, host_cs))
    {
        boot_stubs::log("hvruntime: InitializeSharedContext FAILED -- system incompatible");
        hvmem::release(root_va, root_bytes);
        hv_platform::leave_boot_time();
        return kEfiAborted;
    }

    if (hdr->netlog.enable && hdr->netlog.mode != NETLOG_MODE_BOOT_ONLY && hdr->netlog.mcfg_base_pa &&
        hdr->netlog.hv_bdf)
    {
        SnpSendShutdown();
        boot_stubs::log("hvruntime: SNP shutdown -- NIC handed to bare-metal net_log");
    }

    fb_panic::markf(fb_panic::kSlotNetLog, "NETLOG: INIT ENTERING");
    if (net_log::init(hdr->netlog, bs))
    {
        boot_stubs::log("hvruntime: net_log post-boot exfil armed");
        fb_panic::markf(fb_panic::kSlotNetLog, "NETLOG: ARMED");
    }
    else
    {
        boot_stubs::log("hvruntime: net_log init skipped or failed");
        fb_panic::markf(fb_panic::kSlotNetLog, "NETLOG: INACTIVE (SKIPPED/FAILED)");
    }

    boot_stubs::log("hvruntime: entering vmexit_log::init (%u slots, %llu KB)",
                    vmexit_log::ring_capacity,
                    (unsigned long long)(sizeof(vmexit_log::entry_t) * vmexit_log::ring_capacity) >> 10);
    if (!vmexit_log::init())
    {
        boot_stubs::log("hvruntime: vmexit_log::init FAILED -- log unavailable");
    }
    else
    {
        boot_stubs::log("hvruntime: vmexit_log armed");
    }

    fb_panic::markf(fb_panic::kSlotSelfVirt, "SELF-VIRT: BSP ENTERING");
    VirtualizeProcessor(g_root, 0);
    fb_panic::markf(fb_panic::kSlotSelfVirt, "SELF-VIRT: BSP GUEST");
    boot_stubs::log("hvruntime: BSP virtualized");

#if !HV_FEATURE_VIRTUALIZE_APS
    boot_stubs::log("hvruntime: AP virtualization SKIPPED (HV_FEATURE_VIRTUALIZE_APS=0) -- BSP-only");
    (void)&virtualize_processor_ap;
    if (false)
#else
    if (procCount > 1)
#endif
    {
        fb_panic::markf(fb_panic::kSlotSelfVirt, "SELF-VIRT: APS ENTERING (%u)", procCount - 1);
        s = g_mp->StartupAllAPs(g_mp, virtualize_processor_ap, 1, nullptr, 0, nullptr, nullptr);
        if (s != 0)
        {
            boot_stubs::log("hvruntime: StartupAllAPs status=0x%llx -- some APs unvirtualized", (unsigned long long)s);
            fb_panic::markf(fb_panic::kSlotSelfVirt, "SELF-VIRT: APS PARTIAL (STATUS=0x%llx)", (unsigned long long)s);
        }
        else
        {
            boot_stubs::log("hvruntime: all %u APs virtualized", procCount - 1);
            fb_panic::markf(fb_panic::kSlotSelfVirt, "SELF-VIRT: ALL %u CPUS GUEST", procCount);
        }
    }

    boot_stubs::log("hvruntime: entering micro_vm::system_init");
    if (!micro_vm::system_init())
    {
        boot_stubs::log("hvruntime: micro_vm::system_init returned false (non-fatal)");
    }
    else
    {
        boot_stubs::log("hvruntime: micro_vm::system_init OK");
    }

    hvmem::stats st_stats = {};
    hvmem::get_stats(&st_stats);
    boot_stubs::log(
        "hvruntime: all-CPU virt complete. hvmem used=0x%llx free=0x%llx", st_stats.used_bytes, st_stats.free_bytes);
    boot_stubs::log("hvruntime: returning EFI_SUCCESS -- loader will chainload Windows");

    fb_panic::markf(fb_panic::kSlotChainload, "CHAINLOAD: HV INIT OK - HANDING TO LOADER");
    SnpSendShutdown();
    hv_platform::detach_system_table();
    return kEfiSuccess;
}
