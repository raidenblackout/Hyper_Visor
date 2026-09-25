

#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct VMCB_CONTROL_AREA
{
    uint16_t InterceptCrRead;
    uint16_t InterceptCrWrite;
    uint16_t InterceptDrRead;
    uint16_t InterceptDrWrite;
    uint32_t InterceptException;
    uint32_t InterceptMisc1;
    uint32_t InterceptMisc2;
    uint8_t  Reserved1[0x03C - 0x014];
    uint16_t PauseFilterThreshold;
    uint16_t PauseFilterCount;
    uint64_t IopmBasePa;
    uint64_t MsrpmBasePa;
    uint64_t TscOffset;
    uint32_t GuestAsid;
    uint32_t TlbControl;
    uint64_t VIntr;
    uint64_t InterruptShadow;
    uint64_t ExitCode;
    uint64_t ExitInfo1;
    uint64_t ExitInfo2;
    uint64_t ExitIntInfo;
    uint64_t NpEnable;
    uint64_t AvicApicBar;
    uint64_t GuestPaOfGhcb;
    uint64_t EventInj;
    uint64_t NCr3;
    uint64_t LbrVirtualizationEnable;
    uint32_t VmcbClean;
    uint32_t Reserved2;
    uint64_t NRip;
    uint8_t  NumOfBytesFetched;
    uint8_t  GuestInstructionBytes[15];
    uint64_t AvicApicBackingPagePointer;
    uint64_t Reserved3;
    uint64_t AvicLogicalTablePointer;
    uint64_t AvicPhysicalTablePointer;
    uint64_t Reserved4;
    uint64_t VmcbSaveStatePointer;
    uint8_t  Reserved5[0x400 - 0x110];
};
static_assert(sizeof(VMCB_CONTROL_AREA) == 0x400, "VMCB_CONTROL_AREA size");

struct VMCB_STATE_SAVE_AREA
{
    uint16_t EsSelector;
    uint16_t EsAttrib;
    uint32_t EsLimit;
    uint64_t EsBase;
    uint16_t CsSelector;
    uint16_t CsAttrib;
    uint32_t CsLimit;
    uint64_t CsBase;
    uint16_t SsSelector;
    uint16_t SsAttrib;
    uint32_t SsLimit;
    uint64_t SsBase;
    uint16_t DsSelector;
    uint16_t DsAttrib;
    uint32_t DsLimit;
    uint64_t DsBase;
    uint16_t FsSelector;
    uint16_t FsAttrib;
    uint32_t FsLimit;
    uint64_t FsBase;
    uint16_t GsSelector;
    uint16_t GsAttrib;
    uint32_t GsLimit;
    uint64_t GsBase;
    uint16_t GdtrSelector;
    uint16_t GdtrAttrib;
    uint32_t GdtrLimit;
    uint64_t GdtrBase;
    uint16_t LdtrSelector;
    uint16_t LdtrAttrib;
    uint32_t LdtrLimit;
    uint64_t LdtrBase;
    uint16_t IdtrSelector;
    uint16_t IdtrAttrib;
    uint32_t IdtrLimit;
    uint64_t IdtrBase;
    uint16_t TrSelector;
    uint16_t TrAttrib;
    uint32_t TrLimit;
    uint64_t TrBase;
    uint8_t  Reserved1[0x0CB - 0x0A0];
    uint8_t  Cpl;
    uint32_t Reserved2;
    uint64_t Efer;
    uint8_t  Reserved3[0x148 - 0x0D8];
    uint64_t Cr4;
    uint64_t Cr3;
    uint64_t Cr0;
    uint64_t Dr7;
    uint64_t Dr6;
    uint64_t Rflags;
    uint64_t Rip;
    uint8_t  Reserved4[0x1D8 - 0x180];
    uint64_t Rsp;
    uint8_t  Reserved5[0x1F8 - 0x1E0];
    uint64_t Rax;
    uint64_t Star;
    uint64_t LStar;
    uint64_t CStar;
    uint64_t SfMask;
    uint64_t KernelGsBase;
    uint64_t SysenterCs;
    uint64_t SysenterEsp;
    uint64_t SysenterEip;
    uint64_t Cr2;
    uint8_t  Reserved6[0x268 - 0x248];
    uint64_t GPat;
    uint64_t DbgCtl;
    uint64_t BrFrom;
    uint64_t BrTo;
    uint64_t LastExcepFrom;
    uint64_t LastExcepTo;
};
static_assert(sizeof(VMCB_STATE_SAVE_AREA) == 0x298, "VMCB_STATE_SAVE_AREA size");

struct VMCB
{
    VMCB_CONTROL_AREA    ControlArea;
    VMCB_STATE_SAVE_AREA StateSaveArea;
    uint8_t              Reserved1[0x1000 - sizeof(VMCB_CONTROL_AREA) - sizeof(VMCB_STATE_SAVE_AREA)];
};
static_assert(sizeof(VMCB) == 0x1000, "VMCB size");

inline void SvmAdvanceRip(VMCB* vmcb, uint64_t fallbackLen)
{
    const uint64_t nrip     = vmcb->ControlArea.NRip;
    vmcb->StateSaveArea.Rip = (nrip != 0) ? nrip : (vmcb->StateSaveArea.Rip + fallbackLen);
}

#pragma pack(pop)

#define SVM_INTERCEPT_MISC1_NMI (1UL << 3)

#define SVM_INTERCEPT_MISC1_VINTR (1UL << 4)
#define SVM_INTERCEPT_MISC1_CPUID (1UL << 18)

#define SVM_INTERCEPT_MISC1_PAUSE    (1UL << 23)
#define SVM_INTERCEPT_MISC1_MSR_PROT (1UL << 28)

#define SVM_INTERCEPT_MISC1_HLT      (1UL << 24)
#define SVM_INTERCEPT_MISC1_SHUTDOWN (1UL << 31)
#define SVM_INTERCEPT_MISC2_VMRUN    (1UL << 0)
#define SVM_INTERCEPT_MISC2_VMMCALL  (1UL << 1)

#define SVM_INTERCEPT_MISC2_VMLOAD (1UL << 2)
#define SVM_INTERCEPT_MISC2_VMSAVE (1UL << 3)
#define SVM_INTERCEPT_MISC2_STGI   (1UL << 4)
#define SVM_INTERCEPT_MISC2_CLGI   (1UL << 5)
#define SVM_NP_ENABLE_NP_ENABLE    (1UL << 0)

#define SVM_VINTR_V_IRQ     (1ULL << 8)
#define SVM_VINTR_V_IGN_TPR (1ULL << 20)

#define SVM_VINTR_V_INTR_MASKING (1ULL << 24)
#define SVM_VINTR_PRIO_SHIFT     16
#define SVM_VINTR_VECTOR_SHIFT   32

#define VMEXIT_EXCEPTION_DB 0x0041
#define VMEXIT_EXCEPTION_SX 0x005E
#define VMEXIT_INTR         0x0060
#define VMEXIT_VINTR        0x0064
#define VMEXIT_CPUID        0x0072
#define VMEXIT_PAUSE        0x0077
#define VMEXIT_HLT          0x0078
#define VMEXIT_MSR          0x007C
#define VMEXIT_SHUTDOWN     0x007F
#define VMEXIT_VMRUN        0x0080
#define VMEXIT_VMMCALL      0x0081
#define VMEXIT_VMLOAD       0x0082
#define VMEXIT_VMSAVE       0x0083
#define VMEXIT_STGI         0x0084
#define VMEXIT_CLGI         0x0085
#define VMEXIT_NPF          0x0400
#define VMEXIT_INVALID      (-1LL)
#define VMEXIT_BUSY         (-2LL)
