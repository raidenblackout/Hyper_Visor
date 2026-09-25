#include "vmexit_log.h"
#include "hvmem/hvmem.h"
#include "common.h"
#include <intrin.h>

namespace vmexit_log
{

namespace
{

static entry_t*           g_ring = nullptr;
static volatile long long g_head = 0;

static_assert((ring_capacity & (ring_capacity - 1)) == 0, "ring_capacity must be a power of two for masked indexing");

}

bool init()
{
    if (g_ring)
        return true;
    const size_t bytes = static_cast<size_t>(ring_capacity) * sizeof(entry_t);

    void* va = hvmem::allocate(bytes, HV_PAGE_SIZE);
    if (!va)
        return false;
    g_ring = reinterpret_cast<entry_t*>(va);
    return true;
}

void record(uint32_t cpu, uint32_t code, uint64_t rip, uint64_t info1, uint64_t info2, uint64_t rax, uint64_t rcx)
{
    if (!g_ring)
        return;
    const long long idx = _InterlockedIncrement64(&g_head) - 1;
    entry_t&        e   = g_ring[static_cast<uint64_t>(idx) & (ring_capacity - 1)];

    e.seq = 0;
    _mm_sfence();
    e.tsc   = __rdtsc();
    e.cpu   = cpu;
    e.code  = code;
    e.rip   = rip;
    e.info1 = info1;
    e.info2 = info2;
    e.rax   = rax;
    e.rcx   = rcx;
    _mm_sfence();
    e.seq = static_cast<uint64_t>(idx) + 1;
}

uint64_t head_snapshot()
{
    return static_cast<uint64_t>(_InterlockedOr64(&g_head, 0));
}

uint32_t drain(entry_t* out, uint32_t max_entries, uint64_t* cursor, uint64_t* dropped_out)
{
    if (!out || max_entries == 0 || !cursor)
        return 0;
    if (!g_ring)
    {
        if (dropped_out)
            *dropped_out = 0;
        return 0;
    }

    const uint64_t head = head_snapshot();
    uint64_t       pos  = *cursor;

    if (pos == 0)
    {
        pos = (head > ring_capacity) ? head - ring_capacity : 0;
    }
    else if (pos == ~uint64_t{ 0 })
    {
        pos = head;
    }

    uint64_t dropped = 0;
    if (head > pos + ring_capacity)
    {
        dropped = (head - ring_capacity) - pos;
        pos     = head - ring_capacity;
    }

    if (head == 0 || pos >= head)
    {
        *cursor = pos;
        if (dropped_out)
            *dropped_out = dropped;
        return 0;
    }

    const uint64_t available = head - pos;
    const uint32_t to_scan   = static_cast<uint32_t>(available < max_entries ? available : max_entries);

    uint32_t emitted = 0;
    for (uint32_t i = 0; i < to_scan; ++i)
    {
        const uint64_t slot_idx = pos + i;
        const entry_t& src      = g_ring[slot_idx & (ring_capacity - 1)];
        if (src.seq != slot_idx + 1)
        {
            break;
        }
        out[emitted] = src;
        ++emitted;
    }
    *cursor = pos + emitted;
    if (dropped_out)
        *dropped_out = dropped;
    return emitted;
}

}
