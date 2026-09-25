

#include <ntddk.h>
#include <intrin.h>

#include "svm_probe.h"
#include "common.h"
#include "svm/cpuid.h"
#include "svm/msr.h"
#include "boot_stubs.h"

extern "C" uint32_t g_hv_svm_max_asid      = 0;
extern "C" bool     g_hv_svm_eraps_capable = false;

static bool IsAmdCpu()
{
    int regs[4] = {};
    __cpuid(regs, CPUID_SIGNATURE);
    return (uint32_t)regs[1] == CPUID_SIGNATURE_AUTHENTIC_AMD_EBX &&
           (uint32_t)regs[2] == CPUID_SIGNATURE_AUTHENTIC_AMD_ECX &&
           (uint32_t)regs[3] == CPUID_SIGNATURE_AUTHENTIC_AMD_EDX;
}

static bool ProbeSvmCapsAndCheckCompatible()
{
    if (!IsAmdCpu())
    {
        boot_stubs::log("svm_init: not an AMD processor");
        return false;
    }

    int regs[4] = {};
    __cpuid(regs, CPUID_EXTENDED_CPU_SIG);
    const uint32_t ext_ecx = static_cast<uint32_t>(regs[2]);
    const uint32_t ext_edx = static_cast<uint32_t>(regs[3]);
    (void)ext_edx;
    if ((ext_ecx & CPUID_EXTENDED_CPU_SIG_ECX_SVM) == 0)
    {
        boot_stubs::log("svm_init: AMD-V (SVM) not supported by this CPU (ext_ecx=0x%x)", ext_ecx);
        return false;
    }

    __cpuid(regs, CPUID_SVM_FEATURES);
    const uint32_t svm_rev = static_cast<uint32_t>(regs[0]) & 0xFFu;
    const uint32_t svm_ebx = static_cast<uint32_t>(regs[1]);
    const uint32_t svm_ecx = static_cast<uint32_t>(regs[2]);
    const uint32_t svm_edx = static_cast<uint32_t>(regs[3]);
    g_hv_svm_max_asid      = svm_ebx;

    if ((svm_edx & CPUID_SVM_FEATURES_EDX_NP) == 0)
    {
        boot_stubs::log("svm_init: Nested paging not supported by this CPU (svm_edx=0x%x)", svm_edx);
        return false;
    }

    int ext2[4] = {};
    __cpuid(ext2, 0x80000021);
    const uint32_t ext2_eax = static_cast<uint32_t>(ext2[0]);
    g_hv_svm_eraps_capable  = (ext2_eax & (1u << 24)) != 0;

    const uint64_t vm_cr = __readmsr(SVM_MSR_VM_CR);
    const uint64_t efer  = __readmsr(MSR_IA32_EFER);

    boot_stubs::log("svm_init: CPUID 8000_000A rev=%u nasid=%u edx=0x%08x "
                    "(NP=%u LbrVirt=%u SVML=%u NRIPS=%u TscRateMsr=%u FlushByAsid=%u "
                    "VmcbClean=%u FlushByASID2=%u DecodeAssists=%u PauseFilter=%u "
                    "PauseFilterThreshold=%u VGIF=%u VNMI=%u VmsaveVirt=%u AVIC=%u)",
                    svm_rev,
                    svm_ebx,
                    svm_edx,
                    (svm_edx >> 0) & 1u,
                    (svm_edx >> 1) & 1u,
                    (svm_edx >> 2) & 1u,
                    (svm_edx >> 3) & 1u,
                    (svm_edx >> 4) & 1u,
                    (svm_edx >> 6) & 1u,
                    (svm_edx >> 5) & 1u,
                    (svm_edx >> 6) & 1u,
                    (svm_edx >> 7) & 1u,
                    (svm_edx >> 10) & 1u,
                    (svm_edx >> 12) & 1u,
                    (svm_edx >> 16) & 1u,
                    (svm_edx >> 25) & 1u,
                    (svm_edx >> 15) & 1u,
                    (svm_edx >> 13) & 1u);

    boot_stubs::log("svm_init: CPUID 8000_000A ecx=0x%08x  CPUID 8000_0021 eax=0x%08x (ERAPS=%u)",
                    svm_ecx,
                    ext2_eax,
                    (unsigned)g_hv_svm_eraps_capable);

    boot_stubs::log("svm_init: VM_CR=0x%llx (LOCK=%u SVMDIS=%u DIS_A20M=%u R_INIT=%u DPD=%u)  "
                    "EFER=0x%llx (SVME=%u NXE=%u LMA=%u LME=%u AIBRSE=%u EnhancedTlbi=%u)",
                    (unsigned long long)vm_cr,
                    (unsigned)((vm_cr >> 3) & 1),
                    (unsigned)((vm_cr >> 4) & 1),
                    (unsigned)((vm_cr >> 2) & 1),
                    (unsigned)((vm_cr >> 1) & 1),
                    (unsigned)((vm_cr >> 0) & 1),
                    (unsigned long long)efer,
                    (unsigned)((efer >> 12) & 1),
                    (unsigned)((efer >> 11) & 1),
                    (unsigned)((efer >> 10) & 1),
                    (unsigned)((efer >> 8) & 1),
                    (unsigned)((efer >> 21) & 1),
                    (unsigned)((efer >> 24) & 1));

    if ((vm_cr & VM_CR_SVMDIS) != 0)
    {
        boot_stubs::log("svm_init: VM_CR.SVMDIS is set -- SVM disabled in firmware");
        return false;
    }

    return true;
}

bool IsSystemCompatible()
{
    return ProbeSvmCapsAndCheckCompatible();
}
