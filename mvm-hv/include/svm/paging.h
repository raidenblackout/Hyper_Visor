

#pragma once
#include <cstdint>

#define PAGE_TABLE_ENTRY_COUNT 512
#define SVM_PAGE_SHIFT         12
#define SVM_PAGE_SIZE          0x1000ULL
#define SVM_PAGE_MASK          (SVM_PAGE_SIZE - 1)
#define SVM_LARGE_PAGE_SHIFT   21
#define SVM_LARGE_PAGE_SIZE    (1ULL << SVM_LARGE_PAGE_SHIFT)

#pragma pack(push, 1)

union ADDRESS_TRANSLATION_HELPER
{
    struct
    {
        uint64_t Unused : 12;
        uint64_t Pt     : 9;
        uint64_t Pd     : 9;
        uint64_t Pdpt   : 9;
        uint64_t Pml4   : 9;
    } AsIndex;
    uint64_t AsUInt64;
};

union PML4_ENTRY_2MB
{
    struct
    {
        uint64_t Valid           : 1;
        uint64_t Write           : 1;
        uint64_t User            : 1;
        uint64_t WriteThrough    : 1;
        uint64_t CacheDisable    : 1;
        uint64_t Accessed        : 1;
        uint64_t Reserved1       : 3;
        uint64_t Avl             : 3;
        uint64_t PageFrameNumber : 40;
        uint64_t Reserved2       : 11;
        uint64_t NoExecute       : 1;
    } Bits;
    uint64_t Uint64;
};
using PDP_ENTRY_2MB = PML4_ENTRY_2MB;

union PD_ENTRY_2MB
{
    struct
    {
        uint64_t Valid           : 1;
        uint64_t Write           : 1;
        uint64_t User            : 1;
        uint64_t WriteThrough    : 1;
        uint64_t CacheDisable    : 1;
        uint64_t Accessed        : 1;
        uint64_t Dirty           : 1;
        uint64_t LargePage       : 1;
        uint64_t Global          : 1;
        uint64_t Avl             : 3;
        uint64_t Pat             : 1;
        uint64_t Reserved1       : 8;
        uint64_t PageFrameNumber : 31;
        uint64_t Reserved2       : 11;
        uint64_t NoExecute       : 1;
    } Bits;
    uint64_t Uint64;
};

union PT_ENTRY_4KB
{
    struct
    {
        uint64_t Valid           : 1;
        uint64_t Write           : 1;
        uint64_t User            : 1;
        uint64_t WriteThrough    : 1;
        uint64_t CacheDisable    : 1;
        uint64_t Accessed        : 1;
        uint64_t Dirty           : 1;
        uint64_t Pat             : 1;
        uint64_t Global          : 1;
        uint64_t Avl             : 3;
        uint64_t PageFrameNumber : 40;
        uint64_t Reserved2       : 11;
        uint64_t NoExecute       : 1;
    } Bits;
    uint64_t Uint64;
};

static_assert(sizeof(PML4_ENTRY_2MB) == 8, "");
static_assert(sizeof(PD_ENTRY_2MB) == 8, "");
static_assert(sizeof(PT_ENTRY_4KB) == 8, "");

#define PAGING_PML4_COUNT 2

struct PAGING_STRUCTURES
{
    PML4_ENTRY_2MB Pml4[PAGE_TABLE_ENTRY_COUNT];
    PDP_ENTRY_2MB  Pdpt[PAGING_PML4_COUNT][PAGE_TABLE_ENTRY_COUNT];
    PD_ENTRY_2MB   Pd[PAGING_PML4_COUNT][PAGE_TABLE_ENTRY_COUNT][PAGE_TABLE_ENTRY_COUNT];
};
static_assert((sizeof(PAGING_STRUCTURES) & 0xFFF) == 0, "PAGING_STRUCTURES must be 4KB-multiple");

#pragma pack(pop)
