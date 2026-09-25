

#include <ntddk.h>
#include <intrin.h>

#include "host_state.h"
#include "asm/vm_intrin.h"
#include "npt_setup.h"
#include "boot_stubs.h"

extern "C" void AsmDefaultExceptionHandlers(void);

static constexpr uint64_t kHandlerStubBytes = 12;

static void SetupHostIdt(SHARED_HOST_DATA* shd, uint16_t hostCs)
{
    const uint64_t base = reinterpret_cast<uint64_t>(&AsmDefaultExceptionHandlers);

    for (uint32_t i = 0; i < IDT_ENTRY_COUNT; ++i)
    {
        const uint64_t handler = base + i * kHandlerStubBytes;
        auto&          g       = shd->Idt[i];
        g.Uint64.Low           = 0;
        g.Uint64.High          = 0;
        g.Bits.OffsetLow       = static_cast<uint16_t>(handler);
        g.Bits.Selector        = hostCs;
        g.Bits.Ist             = 0;
        g.Bits.GateType        = 0xE;
        g.Bits.System          = 0;
        g.Bits.Dpl             = 0;
        g.Bits.Present         = 1;
        g.Bits.OffsetHigh      = static_cast<uint16_t>(handler >> 16);
        g.Bits.OffsetUpper     = static_cast<uint32_t>(handler >> 32);
    }

    shd->Idtr.Base  = reinterpret_cast<uint64_t>(&shd->Idt[0]);
    shd->Idtr.Limit = sizeof(shd->Idt) - 1;
}

void InitializeHostSharedData(SHARED_HOST_DATA* shd, uint32_t procCount, uint16_t hostCs)
{
    shd->ProcessorCount = procCount;
    SetupIdentityMapping(&shd->PagingStructures, false);
    shd->Cr3 = reinterpret_cast<uint64_t>(&shd->PagingStructures.Pml4[0]);

    SetupHostIdt(shd, hostCs);
}

void InitializeHostData(HOST_DATA* hd, const SHARED_HOST_DATA* shd)
{
    __stosb((unsigned char*)hd, 0, sizeof(*hd));
    hd->Shared = shd;

    svm_descriptor_table_t gdtr = {};
    svm_sgdt(&gdtr);
    if ((uint64_t)gdtr.limit + 1 > sizeof(hd->Gdt))
    {
        boot_stubs::halt("svm_init: guest GDT too large to clone");
    }
    __movsb((unsigned char*)hd->Gdt, (const unsigned char*)gdtr.base, (size_t)gdtr.limit + 1);
    hd->Gdtr.Base  = reinterpret_cast<uint64_t>(&hd->Gdt[0]);
    hd->Gdtr.Limit = gdtr.limit;
}

void SwitchToHostContext(const SHARED_HOST_DATA* shd, const HOST_DATA* hd)
{
    __writecr3(shd->Cr3);
    svm_lidt(reinterpret_cast<const svm_descriptor_table_t*>(&shd->Idtr));
    svm_lgdt(reinterpret_cast<const svm_descriptor_table_t*>(&hd->Gdtr));
}
