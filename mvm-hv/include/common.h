

#pragma once
#include <ntddk.h>
#include <cstdint>
#include <cstddef>

#include "svm/paging.h"
#include "svm/vmcb.h"
#include "svm/x64.h"

#define VMM_TAG 'svhs'

#define HV_PAGE_SIZE 0x1000ULL

#define NPT_ROOT_COUNT     2
#define MAX_HIDDEN_REGIONS 20

#define HV_FEATURE_LAPIC_INTERCEPT   1
#define HV_FEATURE_INIT_REDIRECT     1
#define HV_FEATURE_LBR               1
#define HV_FEATURE_SBRUN_EBS_FIXUP   1
#define HV_SBRUN_CACHEDRT_PA         0x785B8478ULL
#define HV_SBRUN_GRT_PA              0x785B8330ULL
#define HV_SBRUN_INITFLAG_PA         0x785B82F1ULL
#define HV_FEATURE_LAPIC_AUTODISABLE 1
#define HV_FEATURE_HVB_NPT_PROTECT   1
#define HV_FEATURE_VIRTUALIZE_APS    1
#define HV_MAX_VIRT_APS              0
#define HV_FEATURE_SVM_GUARD         1
#define HV_FEATURE_HOST_WATCH        1
#define HV_FEATURE_VMMCALL           1
#define HV_BASELINE_MSR_PASSTHROUGH  1

#define HV_FEATURE_VMEXIT_LOG 1

#include "hv_toggles.h"

#define HV_SVM_TLB_CTRL_CMD_MASK           0xFFu
#define HV_SVM_TLB_CTRL_CMD_FLUSH_ALL      0x01u
#define HV_SVM_TLB_CTRL_CMD_FLUSH_GUEST    0x03u
#define HV_SVM_TLB_CTRL_CMD_FLUSH_GUEST_NG 0x07u
#define HV_SVM_TLB_CTRL_ALLOW_LARGER_RAP   (1u << 8)
#define HV_SVM_TLB_CTRL_CLEAR_RAP          (1u << 9)

#define HV_INTERCEPT_EXCEPTION_FIXED (1UL << 30)

#define HV_INTERCEPT_MISC1(procNum) (SVM_INTERCEPT_MISC1_PAUSE | SVM_INTERCEPT_MISC1_SHUTDOWN)

#define HV_INTERCEPT_MISC2                                                                  \
    (SVM_INTERCEPT_MISC2_VMRUN | SVM_INTERCEPT_MISC2_VMMCALL | SVM_INTERCEPT_MISC2_VMLOAD | \
     SVM_INTERCEPT_MISC2_VMSAVE | SVM_INTERCEPT_MISC2_STGI | SVM_INTERCEPT_MISC2_CLGI)

extern "C" uint32_t g_intercept_misc1;
extern "C" uint32_t g_intercept_misc2;
extern "C" uint32_t g_intercept_exception;

struct HOST_STACK_BASED_PARAMETERS
{
    uint64_t             GuestVmcbPa;
    struct ROOT_CONTEXT* SharedContext;
    uint32_t             ProcessorNumber;
    uint32_t             Reserved1;
};

#define HOST_STACK_SIZE 0x8000

#define HOST_STACK_PARAMS_SENTINEL 0xFFFFFFFFu

#define HOST_STACK_CANARY 0xC0FFEE5AA5C0FFEEULL

union HOST_STACK
{
    uint8_t Raw[HOST_STACK_SIZE];
    struct
    {
        uint8_t                     AvailableAsStack[HOST_STACK_SIZE - sizeof(HOST_STACK_BASED_PARAMETERS)];
        HOST_STACK_BASED_PARAMETERS Params;
    } Layout;
};
static_assert(sizeof(HOST_STACK) == HOST_STACK_SIZE, "HOST_STACK size");

struct GUEST_REGISTERS
{
    uint64_t R15;
    uint64_t R14;
    uint64_t R13;
    uint64_t R12;
    uint64_t R11;
    uint64_t R10;
    uint64_t R9;
    uint64_t R8;
    uint64_t Rdi;
    uint64_t Rsi;
    uint64_t Rbp;
    uint64_t Rbx;
    uint64_t Rdx;
    uint64_t Rcx;
    uint64_t Rax;
};

enum GUEST_ACTIVITY_STATE : uint32_t
{
    GuestStateActive      = 0,
    GuestStateWaitForSipi = 1,
    GuestStateSipiIssued  = 2,
};

enum APIC_ACCESS_STATE : uint32_t
{
    ApicAccessPassthrough = 0,
    ApicAccessPending     = 1,
};

struct GUEST_STATE
{
    uint32_t                      ProcessorNumber;
    uint32_t                      ApicId;
    volatile GUEST_ACTIVITY_STATE ActivityState;
    APIC_ACCESS_STATE             ApicAccessState;
    GUEST_REGISTERS*              Registers;
    uint8_t                       SipiVector;
    uint8_t                       DiscardInitSignal;

    volatile uint8_t Virtualized;
    uint8_t          Reserved0;
};

struct SVM_STATE
{
    PT_ENTRY_4KB* LocalApicNestedPte;
};

struct CONTEXT_COLLECTION
{
    GUEST_STATE Guest;
    SVM_STATE   Svm;
    uint8_t     Reserved1[HV_PAGE_SIZE - sizeof(GUEST_STATE) - sizeof(SVM_STATE)];
};
static_assert((sizeof(CONTEXT_COLLECTION) % HV_PAGE_SIZE) == 0, "CONTEXT_COLLECTION 4KB");

#define IDT_ENTRY_COUNT 256

struct SHARED_HOST_DATA
{
    PAGING_STRUCTURES        PagingStructures;
    IA32_IDT_GATE_DESCRIPTOR Idt[IDT_ENTRY_COUNT];
    IA32_DESCRIPTOR          Idtr;
    uint8_t                  Reserved0[2];
    uint32_t                 ProcessorCount;
    uint64_t                 Cr3;
    uint8_t                  Reserved1[HV_PAGE_SIZE - sizeof(IA32_DESCRIPTOR) - 2 - 4 - 8];
};
static_assert((sizeof(SHARED_HOST_DATA) % HV_PAGE_SIZE) == 0, "SHARED_HOST_DATA 4KB");
static_assert((offsetof(SHARED_HOST_DATA, Idt) % HV_PAGE_SIZE) == 0, "SHARED_HOST_DATA.Idt 4KB");

struct HOST_DATA
{
    const SHARED_HOST_DATA* Shared;
    IA32_SEGMENT_DESCRIPTOR Gdt[16];
    IA32_DESCRIPTOR         Gdtr;
    IA32_TASK_STATE_SEGMENT Tss;
    uint8_t                 Reserved1[HV_PAGE_SIZE - 8 - 128 - 10 - 104];
};
static_assert((sizeof(HOST_DATA) % HV_PAGE_SIZE) == 0, "HOST_DATA 4KB");

struct PER_CPU_DATA
{
    CONTEXT_COLLECTION States;
    VMCB               GuestVmcb;
    VMCB               HostVmcb;
    HOST_STACK         HostStack;
    uint8_t            HostStateArea[HV_PAGE_SIZE];
    HOST_DATA          HostX64Data;
};
static_assert((sizeof(PER_CPU_DATA) % HV_PAGE_SIZE) == 0, "PER_CPU_DATA 4KB");
static_assert((offsetof(PER_CPU_DATA, GuestVmcb) % HV_PAGE_SIZE) == 0, "GuestVmcb 4KB");
static_assert((offsetof(PER_CPU_DATA, HostVmcb) % HV_PAGE_SIZE) == 0, "HostVmcb 4KB");
static_assert((offsetof(PER_CPU_DATA, HostStack) % HV_PAGE_SIZE) == 0, "HostStack 4KB");
static_assert((offsetof(PER_CPU_DATA, HostStateArea) % HV_PAGE_SIZE) == 0, "HostStateArea 4KB");

struct SHARED_SVM_DATA
{
    PAGING_STRUCTURES NestedPageTables;
    PAGING_STRUCTURES NestedPageTablesForBsp;
    PT_ENTRY_4KB      NestedPtForBsp[PAGE_TABLE_ENTRY_COUNT];

    PT_ENTRY_4KB WatchPtForBsp[PAGE_TABLE_ENTRY_COUNT];
    uint8_t      ShadowLocalApicPage[HV_PAGE_SIZE];

    uint8_t MsrPermissionsBitmap[0x2000];

    PT_ENTRY_4KB HostWatchPt[2][PAGE_TABLE_ENTRY_COUNT];
    PT_ENTRY_4KB HiddenPt[NPT_ROOT_COUNT][MAX_HIDDEN_REGIONS][PAGE_TABLE_ENTRY_COUNT];
};
static_assert((sizeof(SHARED_SVM_DATA) % HV_PAGE_SIZE) == 0, "SHARED_SVM_DATA 4KB");
static_assert((offsetof(SHARED_SVM_DATA, NestedPageTablesForBsp) % HV_PAGE_SIZE) == 0, "NestedPageTablesForBsp 4KB");
static_assert((offsetof(SHARED_SVM_DATA, NestedPtForBsp) % HV_PAGE_SIZE) == 0, "NestedPtForBsp 4KB");
static_assert((offsetof(SHARED_SVM_DATA, ShadowLocalApicPage) % HV_PAGE_SIZE) == 0, "ShadowLocalApicPage 4KB");
static_assert((offsetof(SHARED_SVM_DATA, MsrPermissionsBitmap) % HV_PAGE_SIZE) == 0, "MsrPermissionsBitmap 4KB");
static_assert((offsetof(SHARED_SVM_DATA, HiddenPt) % HV_PAGE_SIZE) == 0, "HiddenPt 4KB");

struct ROOT_CONTEXT
{
    SHARED_SVM_DATA  Svm;
    SHARED_HOST_DATA HostX64;
    PER_CPU_DATA     Cpus[1];
};
static_assert((offsetof(ROOT_CONTEXT, HostX64) % HV_PAGE_SIZE) == 0, "ROOT_CONTEXT.HostX64 4KB");
static_assert((offsetof(ROOT_CONTEXT, Cpus) % HV_PAGE_SIZE) == 0, "ROOT_CONTEXT.Cpus 4KB");

inline size_t root_context_bytes(uint32_t cpu_count)
{
    return sizeof(SHARED_SVM_DATA) + sizeof(SHARED_HOST_DATA) + (size_t)cpu_count * sizeof(PER_CPU_DATA);
}

extern "C" uint32_t g_hv_svm_max_asid;
extern "C" bool     g_hv_svm_eraps_capable;

struct HOST_CONTEXT
{
    PER_CPU_DATA* Cpu;
    ROOT_CONTEXT* Root;
};
