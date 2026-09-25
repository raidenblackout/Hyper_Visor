

#include "hvmem/hvmem.h"
#include "hvmem/platform.h"

#include <stdint.h>

namespace hvmem
{

static inline uint64_t align_up_u64(uint64_t v, uint64_t a)
{
    return (v + a - 1) & ~(a - 1);
}
static inline bool is_power_of_two(uint64_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static uint32_t size_to_class(uint64_t n)
{
    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; ++i)
        if (SLAB_CLASS_SIZES[i] >= n)
            return i;
    return SLAB_CLASS_COUNT;
}

struct slab_page_header
{
    uint32_t                 chunk_size;
    uint16_t                 chunks_per_page;
    uint16_t                 free_count;
    uint16_t                 free_head;
    uint16_t                 class_index;
    struct slab_page_header* next;
    uint64_t                 _pad;
};
static_assert(sizeof(slab_page_header) == 32, "slab header size drift");

static inline slab_page_header* header_from_page(uint8_t* page)
{
    return reinterpret_cast<slab_page_header*>(page + PAGE_BYTES - sizeof(slab_page_header));
}
static inline uint8_t* page_from_chunk(void* chunk)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(chunk) & ~static_cast<uintptr_t>(PAGE_BYTES - 1));
}

struct alignas(platform::SPINLOCK_WORD_ALIGN) lock_slot
{
    uint8_t storage[platform::SPINLOCK_WORD_SIZE];
};

struct slab_class
{
    slab_page_header* head;
    lock_slot         lock;
};

namespace
{
void*    g_base_va = nullptr;
uint64_t g_base_pa = 0;
uint64_t g_size    = 0;

uint64_t* g_bitmap         = nullptr;
uint64_t  g_bitmap_words   = 0;
uint64_t  g_total_pages    = 0;
uint64_t  g_reserved_pages = 0;
void*     g_page_pool_va   = nullptr;
lock_slot g_bitmap_lock;

slab_class g_slab[SLAB_CLASS_COUNT];

uint64_t g_page_alloc_count = 0;
uint64_t g_page_free_count  = 0;
uint64_t g_slab_alloc_count = 0;
uint64_t g_slab_free_count  = 0;
uint64_t g_used_pages       = 0;

bool g_ready = false;
}

static void zero_bytes(void* dst, uint64_t n)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    while (n >= 8)
    {
        *reinterpret_cast<uint64_t*>(p)  = 0;
        p                               += 8;
        n                               -= 8;
    }
    while (n--)
    {
        *p++ = 0;
    }
}

static inline bool bit_test(uint64_t idx)
{
    return (g_bitmap[idx >> 6] >> (idx & 63)) & 1ull;
}
static inline void bit_set(uint64_t idx)
{
    g_bitmap[idx >> 6] |= (1ull << (idx & 63));
}
static inline void bit_clear(uint64_t idx)
{
    g_bitmap[idx >> 6] &= ~(1ull << (idx & 63));
}

static uint64_t bitmap_find_run(uint64_t count, uint64_t align_pages)
{
    if (count == 0)
        return UINT64_MAX;
    uint64_t start = g_reserved_pages;

    start = align_up_u64(start, align_pages);
    while (start + count <= g_total_pages)
    {
        uint64_t i = 0;
        while (i < count && !bit_test(start + i))
            ++i;
        if (i == count)
            return start;

        start = align_up_u64(start + i + 1, align_pages);
    }
    return UINT64_MAX;
}

static void bitmap_mark_used(uint64_t start, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i)
        bit_set(start + i);
    g_used_pages += count;
}
static void bitmap_mark_free(uint64_t start, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i)
        bit_clear(start + i);
    g_used_pages -= count;
}

static void* pages_alloc(uint32_t count, uint32_t align_pages)
{
    if (count == 0 || !is_power_of_two(align_pages))
        return nullptr;

    platform::spin_acquire(&g_bitmap_lock);
    uint64_t idx = bitmap_find_run(count, align_pages);
    if (idx == UINT64_MAX)
    {
        platform::spin_release(&g_bitmap_lock);
        return nullptr;
    }
    bitmap_mark_used(idx, count);
    g_page_alloc_count += 1;
    platform::spin_release(&g_bitmap_lock);

    void* va = static_cast<uint8_t*>(g_page_pool_va) + idx * PAGE_BYTES;
    zero_bytes(va, count * PAGE_BYTES);
    return va;
}

static void pages_free(void* ptr, uint32_t count)
{
    if (!ptr || count == 0)
        return;
    uintptr_t v    = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t base = reinterpret_cast<uintptr_t>(g_page_pool_va);

    if (v < base || v >= base + g_size)
        return;
    if ((v - base) & (PAGE_BYTES - 1))
        return;
    uint64_t idx = (v - base) / PAGE_BYTES;
    if (idx + count > g_total_pages)
        return;

    platform::spin_acquire(&g_bitmap_lock);
    bitmap_mark_free(idx, count);
    g_page_free_count += 1;
    platform::spin_release(&g_bitmap_lock);
}

static slab_page_header* slab_page_init(uint8_t* page, uint32_t class_idx)
{
    uint32_t chunk_size = SLAB_CLASS_SIZES[class_idx];

    uint32_t usable = static_cast<uint32_t>(PAGE_BYTES) - sizeof(slab_page_header);
    uint32_t chunks = usable / chunk_size;

    slab_page_header* h = header_from_page(page);
    h->chunk_size       = chunk_size;
    h->chunks_per_page  = static_cast<uint16_t>(chunks);
    h->free_count       = static_cast<uint16_t>(chunks);
    h->free_head        = 0;
    h->class_index      = static_cast<uint16_t>(class_idx);
    h->next             = nullptr;
    h->_pad             = 0;

    for (uint32_t i = 0; i + 1 < chunks; ++i)
    {
        *reinterpret_cast<uint16_t*>(page + i * chunk_size) = static_cast<uint16_t>(i + 1);
    }
    if (chunks > 0)
    {
        *reinterpret_cast<uint16_t*>(page + (chunks - 1) * chunk_size) = static_cast<uint16_t>(0xFFFF);
    }
    return h;
}

static void* slab_page_pop(slab_page_header* h)
{
    if (h->free_count == 0 || h->free_head == 0xFFFF)
        return nullptr;

    uint8_t*  page  = page_from_chunk(h);
    uint16_t  idx   = h->free_head;
    uint16_t* slot  = reinterpret_cast<uint16_t*>(page + idx * h->chunk_size);
    h->free_head    = *slot;
    h->free_count  -= 1;
    return slot;
}

static void slab_page_push(slab_page_header* h, void* chunk)
{
    uint8_t* page = page_from_chunk(chunk);
    uint16_t idx =
        static_cast<uint16_t>((reinterpret_cast<uintptr_t>(chunk) - reinterpret_cast<uintptr_t>(page)) / h->chunk_size);
    *reinterpret_cast<uint16_t*>(chunk)  = h->free_head;
    h->free_head                         = idx;
    h->free_count                       += 1;
}

static slab_page_header* slab_find_free(uint32_t class_idx)
{
    for (slab_page_header* h = g_slab[class_idx].head; h; h = h->next)
    {
        if (h->free_count > 0)
            return h;
    }
    return nullptr;
}

static bool slab_list_detach(uint32_t class_idx, slab_page_header* victim)
{
    slab_page_header** cursor = &g_slab[class_idx].head;
    while (*cursor)
    {
        if (*cursor == victim)
        {
            *cursor = victim->next;
            return true;
        }
        cursor = &(*cursor)->next;
    }
    return false;
}

static void* slab_alloc(uint32_t class_idx)
{
    platform::spin_acquire(&g_slab[class_idx].lock);
    slab_page_header* h = slab_find_free(class_idx);
    if (h)
    {
        void* chunk         = slab_page_pop(h);
        g_slab_alloc_count += 1;
        platform::spin_release(&g_slab[class_idx].lock);
        if (chunk)
            zero_bytes(chunk, SLAB_CLASS_SIZES[class_idx]);
        return chunk;
    }
    platform::spin_release(&g_slab[class_idx].lock);

    uint8_t* page = static_cast<uint8_t*>(pages_alloc(1, 1));
    if (!page)
        return nullptr;

    slab_page_header* fresh = slab_page_init(page, class_idx);

    platform::spin_acquire(&g_slab[class_idx].lock);
    fresh->next             = g_slab[class_idx].head;
    g_slab[class_idx].head  = fresh;
    void* chunk             = slab_page_pop(fresh);
    g_slab_alloc_count     += 1;
    platform::spin_release(&g_slab[class_idx].lock);

    if (chunk)
        zero_bytes(chunk, SLAB_CLASS_SIZES[class_idx]);
    return chunk;
}

static void slab_free(uint32_t class_idx, void* ptr)
{
    if (!ptr)
        return;

    uint8_t*          page = page_from_chunk(ptr);
    slab_page_header* h    = header_from_page(page);

    slab_page_header* to_release = nullptr;

    platform::spin_acquire(&g_slab[class_idx].lock);
    slab_page_push(h, ptr);
    g_slab_free_count += 1;

    const bool fully_empty   = (h->free_count == h->chunks_per_page);
    const bool not_only_slab = (g_slab[class_idx].head != h) || (h->next != nullptr);
    if (fully_empty && not_only_slab)
    {
        if (slab_list_detach(class_idx, h))
            to_release = h;
    }
    platform::spin_release(&g_slab[class_idx].lock);

    if (to_release)
        pages_free(page_from_chunk(to_release), 1);
}

bool initialize(void* base_va, uint64_t base_pa, uint64_t size_bytes)
{
    if (g_ready && base_va == g_base_va && base_pa == g_base_pa && size_bytes == g_size)
        return true;
    if (g_ready)
        return false;

    if (!base_va || base_pa == 0)
        return false;
    if (size_bytes < 4 * PAGE_BYTES)
        return false;
    if (size_bytes & (PAGE_BYTES - 1))
        return false;
    if (base_pa & (PAGE_BYTES - 1))
        return false;

    g_base_va     = base_va;
    g_base_pa     = base_pa;
    g_size        = size_bytes;
    g_total_pages = size_bytes / PAGE_BYTES;

    uint64_t bitmap_bytes = align_up_u64((g_total_pages + 7) / 8, 8);
    uint64_t bitmap_pages = align_up_u64(bitmap_bytes, PAGE_BYTES) / PAGE_BYTES;
    if (bitmap_pages >= g_total_pages)
        return false;

    g_bitmap         = static_cast<uint64_t*>(base_va);
    g_bitmap_words   = bitmap_bytes / 8;
    g_reserved_pages = bitmap_pages;
    g_page_pool_va   = base_va;

    zero_bytes(g_bitmap, bitmap_bytes);

    for (uint64_t i = g_total_pages; i < g_bitmap_words * 64; ++i)
        bit_set(i);

    for (uint64_t i = 0; i < bitmap_pages; ++i)
        bit_set(i);
    g_used_pages = bitmap_pages;

    platform::spin_init(&g_bitmap_lock);
    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; ++i)
    {
        g_slab[i].head = nullptr;
        platform::spin_init(&g_slab[i].lock);
    }

    g_page_alloc_count = 0;
    g_page_free_count  = 0;
    g_slab_alloc_count = 0;
    g_slab_free_count  = 0;
    g_ready            = true;
    return true;
}

void shutdown()
{
    if (!g_ready)
        return;
    g_ready          = false;
    g_base_va        = nullptr;
    g_base_pa        = 0;
    g_size           = 0;
    g_bitmap         = nullptr;
    g_bitmap_words   = 0;
    g_total_pages    = 0;
    g_reserved_pages = 0;
    g_page_pool_va   = nullptr;
    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; ++i)
        g_slab[i].head = nullptr;
    g_used_pages = 0;
}

bool is_ready()
{
    return g_ready;
}

void* allocate(uint64_t size, uint64_t alignment)
{
    if (!g_ready)
        return nullptr;
    if (size == 0)
        return nullptr;
    if (!is_power_of_two(alignment))
        return nullptr;

    if (size >= PAGE_BYTES || alignment >= PAGE_BYTES)
    {
        uint64_t pages       = align_up_u64(size, PAGE_BYTES) / PAGE_BYTES;
        uint64_t align_pages = alignment < PAGE_BYTES ? 1 : (alignment / PAGE_BYTES);
        return pages_alloc(static_cast<uint32_t>(pages), static_cast<uint32_t>(align_pages));
    }

    uint64_t effective = size < alignment ? alignment : size;
    uint32_t cls       = size_to_class(effective);
    if (cls >= SLAB_CLASS_COUNT)
    {
        uint64_t pages = align_up_u64(size, PAGE_BYTES) / PAGE_BYTES;
        return pages_alloc(static_cast<uint32_t>(pages), 1);
    }
    return slab_alloc(cls);
}

void release(void* ptr, uint64_t size)
{
    if (!ptr || !g_ready)
        return;
    if (size == 0)
        return;

    if (size >= PAGE_BYTES)
    {
        uint64_t pages = align_up_u64(size, PAGE_BYTES) / PAGE_BYTES;
        pages_free(ptr, static_cast<uint32_t>(pages));
        return;
    }
    uint32_t cls = size_to_class(size);
    if (cls >= SLAB_CLASS_COUNT)
    {
        uint64_t pages = align_up_u64(size, PAGE_BYTES) / PAGE_BYTES;
        pages_free(ptr, static_cast<uint32_t>(pages));
        return;
    }
    slab_free(cls, ptr);
}

void* allocate_page()
{
    return pages_alloc(1, 1);
}
void* allocate_pages(uint32_t n)
{
    return pages_alloc(n, 1);
}
void release_page(void* p)
{
    pages_free(p, 1);
}
void release_pages(void* p, uint32_t n)
{
    pages_free(p, n);
}

uint64_t virt_to_phys(const void* va)
{
    return g_base_pa + (reinterpret_cast<uintptr_t>(va) - reinterpret_cast<uintptr_t>(g_base_va));
}
void* phys_to_virt(uint64_t pa)
{
    return static_cast<uint8_t*>(g_base_va) + (pa - g_base_pa);
}
bool contains(const void* ptr)
{
    if (!g_ready)
        return false;
    uintptr_t v    = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t base = reinterpret_cast<uintptr_t>(g_base_va);
    return v >= base && v < base + g_size;
}

void get_stats(stats* out)
{
    if (!out)
        return;
    out->total_bytes = g_total_pages * PAGE_BYTES;

    out->used_bytes       = g_used_pages * PAGE_BYTES;
    out->free_bytes       = (g_total_pages - g_used_pages) * PAGE_BYTES;
    out->page_alloc_count = g_page_alloc_count;
    out->page_free_count  = g_page_free_count;
    out->slab_alloc_count = g_slab_alloc_count;
    out->slab_free_count  = g_slab_free_count;

    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; ++i)
    {
        uint32_t sum = 0;
        for (slab_page_header* h = g_slab[i].head; h; h = h->next)
            sum += h->free_count;
        out->slab_class_free[i] = sum;
    }
}
}
