#include "microvm_rt.h"

namespace mvm
{

MicroVmConfig* g_config      = nullptr;
uint64_t       g_heap_cursor = 0;

void Init(MicroVmConfig* config_ptr)
{
    g_config      = config_ptr;
    g_heap_cursor = g_config->heap_base;
}

void* PhysToVirt(uint64_t pa)
{
    return reinterpret_cast<void*>(g_config->physmap_base + pa);
}

volatile void* PhysToVirtIO(uint64_t pa)
{
    return reinterpret_cast<volatile void*>(g_config->physmap_base + pa);
}

void* GetMailbox(uint32_t index)
{
    if (!g_config || index >= g_config->mailbox_count)
        return nullptr;
    return reinterpret_cast<void*>(g_config->mailbox_gvas[index]);
}

uint32_t GetMailboxCount()
{
    return g_config ? g_config->mailbox_count : 0;
}

void* Alloc(uint64_t size)
{
    size         = (size + 15) & ~15ULL;
    uint64_t end = g_config->heap_base + g_config->heap_size;
    if (g_heap_cursor + size > end)
        return nullptr;
    void* ptr      = reinterpret_cast<void*>(g_heap_cursor);
    g_heap_cursor += size;
    return ptr;
}

void Free(void*) {}

uint64_t GetHeapRemaining()
{
    uint64_t end = g_config->heap_base + g_config->heap_size;
    return end > g_heap_cursor ? end - g_heap_cursor : 0;
}

extern "C" void mvm_signal_done();
extern "C" void mvm_signal_yield();

void Done()
{
    mvm_signal_done();
}
void Yield()
{
    mvm_signal_yield();
}

}
