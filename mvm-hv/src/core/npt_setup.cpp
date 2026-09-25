

#include <ntddk.h>
#include <intrin.h>

#include "npt_setup.h"
#include "boot_stubs.h"

extern "C" void split_2mb_pde(PD_ENTRY_2MB* pde, PT_ENTRY_4KB* pt);

void SetupIdentityMapping(PAGING_STRUCTURES* ps, bool nestedPageTables)
{
    __stosb((unsigned char*)ps, 0, sizeof(*ps));

    const uint64_t user = nestedPageTables ? 1ULL : 0ULL;

    uint64_t pa = 0;
    for (uint32_t pml4Idx = 0; pml4Idx < PAGING_PML4_COUNT; ++pml4Idx)
    {
        ps->Pml4[pml4Idx].Bits.Valid           = 1;
        ps->Pml4[pml4Idx].Bits.Write           = 1;
        ps->Pml4[pml4Idx].Bits.User            = user;
        ps->Pml4[pml4Idx].Bits.PageFrameNumber = reinterpret_cast<uint64_t>(&ps->Pdpt[pml4Idx][0]) >> SVM_PAGE_SHIFT;

        for (uint32_t pdptIdx = 0; pdptIdx < PAGE_TABLE_ENTRY_COUNT; ++pdptIdx)
        {
            ps->Pdpt[pml4Idx][pdptIdx].Bits.Valid = 1;
            ps->Pdpt[pml4Idx][pdptIdx].Bits.Write = 1;
            ps->Pdpt[pml4Idx][pdptIdx].Bits.User  = user;
            ps->Pdpt[pml4Idx][pdptIdx].Bits.PageFrameNumber =
                reinterpret_cast<uint64_t>(&ps->Pd[pml4Idx][pdptIdx][0]) >> SVM_PAGE_SHIFT;

            for (uint32_t pdIdx = 0; pdIdx < PAGE_TABLE_ENTRY_COUNT; ++pdIdx)
            {
                auto& pde                 = ps->Pd[pml4Idx][pdptIdx][pdIdx];
                pde.Bits.Valid            = 1;
                pde.Bits.Write            = 1;
                pde.Bits.User             = user;
                pde.Bits.LargePage        = 1;
                pde.Bits.PageFrameNumber  = pa >> SVM_LARGE_PAGE_SHIFT;
                pa                       += SVM_LARGE_PAGE_SIZE;
            }
        }
    }
}

#if HV_FEATURE_HVB_NPT_PROTECT
void ProtectRootContextInNpt(PAGING_STRUCTURES* ps, uint64_t start, uint64_t end)
{
    const uint64_t first = start & ~(SVM_LARGE_PAGE_SIZE - 1);
    const uint64_t last  = (end + SVM_LARGE_PAGE_SIZE - 1) & ~(SVM_LARGE_PAGE_SIZE - 1);
    for (uint64_t pa = first; pa < last; pa += SVM_LARGE_PAGE_SIZE)
    {
        const uint32_t pml4 = static_cast<uint32_t>(pa >> 39);
        const uint32_t pdpt = static_cast<uint32_t>((pa >> 30) & 0x1FF);
        const uint32_t pd   = static_cast<uint32_t>((pa >> 21) & 0x1FF);
        if (pml4 >= PAGING_PML4_COUNT)
            continue;
        ps->Pd[pml4][pdpt][pd].Bits.Write = 0;
    }
}
#endif

#if HV_FEATURE_HOST_WATCH

void ProtectHostStateAreas(ROOT_CONTEXT* root, uint32_t procCount)
{
    PAGING_STRUCTURES* hostPs = &root->HostX64.PagingStructures;

    uint64_t splitBase[2] = { 0, 0 };
    uint32_t nSplit       = 0;
    uint32_t nProtected   = 0;

    for (uint32_t n = 0; n < procCount; ++n)
    {
        const uint64_t pa = reinterpret_cast<uint64_t>(&root->Cpus[n].HostStateArea[0]);

        ADDRESS_TRANSLATION_HELPER h;
        h.AsUInt64 = pa;
        if (h.AsIndex.Pml4 >= PAGING_PML4_COUNT)
        {
            continue;
        }

        PD_ENTRY_2MB*  pde  = &hostPs->Pd[h.AsIndex.Pml4][h.AsIndex.Pdpt][h.AsIndex.Pd];
        const uint64_t base = pa & ~(SVM_LARGE_PAGE_SIZE - 1);

        uint32_t slot     = 0;
        bool     haveSlot = false;
        for (uint32_t i = 0; i < nSplit; ++i)
        {
            if (splitBase[i] == base)
            {
                slot     = i;
                haveSlot = true;
                break;
            }
        }
        if (!haveSlot)
        {
            if (nSplit >= 2)
            {
                boot_stubs::log("svm_init: HOST WATCH -- >2 PDE regions, skipping cpu%u save area", n);
                continue;
            }
            slot            = nSplit++;
            splitBase[slot] = base;
            split_2mb_pde(pde, &root->Svm.HostWatchPt[slot][0]);
        }

        root->Svm.HostWatchPt[slot][h.AsIndex.Pt].Bits.Write = 0;
        ++nProtected;
    }

    boot_stubs::log(
        "svm_init: HOST WATCH armed -- %u HostStateArea pages RO across %u PDE split(s)", nProtected, nSplit);
}
#endif
