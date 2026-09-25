

#include "hvmem/hvmem.h"
#include "hvmem/platform.h"

#include <stdint.h>

namespace hvmem
{

static uint64_t next_pow2(uint64_t n)
{
    uint64_t p = 1;
    while (p < n && p < PAGE_BYTES)
        p <<= 1;
    return p;
}

static bool check_class(uint32_t class_idx)
{
    const uint64_t sz = SLAB_CLASS_SIZES[class_idx];
    void*          p  = allocate(sz, sz);
    if (!p)
    {
        platform::log("selftest: alloc failed for class %u (size %llu)", class_idx, (unsigned long long)sz);
        return false;
    }
    if (reinterpret_cast<uintptr_t>(p) & (next_pow2(sz) - 1))
    {
        platform::log("selftest: class %u alignment miss on %p", class_idx, p);
        release(p, sz);
        return false;
    }

    uint8_t* b = static_cast<uint8_t*>(p);
    if (b[0] != 0 || b[sz - 1] != 0)
    {
        platform::log("selftest: class %u not zeroed", class_idx);
        release(p, sz);
        return false;
    }

    if (!contains(p))
    {
        platform::log("selftest: contains() false for own alloc %p", p);
        release(p, sz);
        return false;
    }
    uint64_t pa = virt_to_phys(p);
    if (phys_to_virt(pa) != p)
    {
        platform::log(
            "selftest: VA/PA round-trip broken (%p -> 0x%llx -> %p)", p, (unsigned long long)pa, phys_to_virt(pa));
        release(p, sz);
        return false;
    }

    for (uint64_t i = 0; i < sz; ++i)
        b[i] = 0xA5;
    release(p, sz);
    return true;
}

static bool check_slab_refill()
{
    constexpr uint32_t N = 300;
    void*              buf[N];
    for (uint32_t i = 0; i < N; ++i)
    {
        buf[i] = allocate(16, 16);
        if (!buf[i])
        {
            platform::log("selftest: slab refill alloc %u failed", i);
            for (uint32_t j = 0; j < i; ++j)
                release(buf[j], 16);
            return false;
        }
    }

    for (uint32_t i = N; i-- > 0;)
        release(buf[i], 16);
    return true;
}

static bool check_page_fragmentation()
{
    constexpr uint32_t N = 16;
    void*              p[N];
    for (uint32_t i = 0; i < N; ++i)
    {
        p[i] = allocate_page();
        if (!p[i])
        {
            platform::log("selftest: page frag setup alloc %u failed", i);
            for (uint32_t j = 0; j < i; ++j)
                release_page(p[j]);
            return false;
        }
    }

    for (uint32_t i = 1; i < N; i += 2)
        release_page(p[i]);

    void* q[N / 2];
    for (uint32_t i = 0; i < N / 2; ++i)
    {
        q[i] = allocate_page();
        if (!q[i])
        {
            platform::log("selftest: page frag refill alloc %u failed", i);
            for (uint32_t j = 0; j < i; ++j)
                release_page(q[j]);
            for (uint32_t j = 0; j < N; j += 2)
                release_page(p[j]);
            return false;
        }
    }

    for (uint32_t i = 0; i < N / 2; ++i)
        release_page(q[i]);
    for (uint32_t i = 0; i < N; i += 2)
        release_page(p[i]);
    return true;
}

static bool check_oom()
{
    stats s{};
    get_stats(&s);

    uint64_t huge_pages = s.total_bytes / PAGE_BYTES + 1;
    void*    p          = allocate_pages(static_cast<uint32_t>(huge_pages));
    if (p)
    {
        platform::log("selftest: OOM returned a pointer");
        release_pages(p, static_cast<uint32_t>(huge_pages));
        return false;
    }
    return true;
}

static bool check_contains_negative()
{
    int stack_probe = 0;
    if (contains(&stack_probe))
    {
        platform::log("selftest: contains() true for stack pointer %p", &stack_probe);
        return false;
    }
    return true;
}

bool selftest()
{
    if (!is_ready())
    {
        platform::log("selftest: arena not ready");
        return false;
    }

    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; ++i)
    {
        if (!check_class(i))
            return false;
    }
    if (!check_slab_refill())
        return false;
    if (!check_page_fragmentation())
        return false;
    if (!check_oom())
        return false;
    if (!check_contains_negative())
        return false;

    stats s{};
    get_stats(&s);
    platform::log("selftest OK: total=%llu MiB used=%llu KiB free=%llu MiB "
                  "page(a=%llu,f=%llu) slab(a=%llu,f=%llu)",
                  (unsigned long long)(s.total_bytes >> 20),
                  (unsigned long long)(s.used_bytes >> 10),
                  (unsigned long long)(s.free_bytes >> 20),
                  (unsigned long long)s.page_alloc_count,
                  (unsigned long long)s.page_free_count,
                  (unsigned long long)s.slab_alloc_count,
                  (unsigned long long)s.slab_free_count);
    return true;
}
}
