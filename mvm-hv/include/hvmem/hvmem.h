

#pragma once

#include <stdint.h>

namespace hvmem
{

inline constexpr uint32_t SLAB_CLASS_COUNT                   = 8;
inline constexpr uint32_t SLAB_CLASS_SIZES[SLAB_CLASS_COUNT] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };

inline constexpr uint64_t PAGE_BYTES = 4096;

bool initialize(void* base_va, uint64_t base_pa, uint64_t size_bytes);

void shutdown();

void* allocate(uint64_t size, uint64_t alignment);

void release(void* ptr, uint64_t size);

void* allocate_page();
void* allocate_pages(uint32_t count);
void  release_page(void* ptr);
void  release_pages(void* ptr, uint32_t count);

template <typename T> inline T* alloc()
{
    return static_cast<T*>(allocate(sizeof(T), alignof(T)));
}
template <typename T> inline void release(T* ptr)
{
    release(static_cast<void*>(ptr), sizeof(T));
}

uint64_t virt_to_phys(const void* va);
void*    phys_to_virt(uint64_t pa);

bool contains(const void* ptr);

struct stats
{
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t page_alloc_count;
    uint64_t page_free_count;
    uint64_t slab_alloc_count;
    uint64_t slab_free_count;
    uint32_t slab_class_free[SLAB_CLASS_COUNT];
};
void get_stats(stats* out);

bool is_ready();

bool selftest();
}
