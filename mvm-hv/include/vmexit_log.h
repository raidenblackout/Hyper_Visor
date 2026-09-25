

#pragma once
#include <cstdint>

namespace vmexit_log
{

struct entry_t
{
    uint64_t tsc;
    uint32_t cpu;
    uint32_t code;
    uint64_t rip;
    uint64_t info1;
    uint64_t info2;
    uint64_t rax;
    uint64_t rcx;
    uint64_t seq;
};
static_assert(sizeof(entry_t) == 64, "entry_t must be one cache line");

constexpr uint32_t ring_capacity = 1 << 20;

bool init();

void record(uint32_t cpu, uint32_t code, uint64_t rip, uint64_t info1, uint64_t info2, uint64_t rax, uint64_t rcx);

uint32_t drain(entry_t* out, uint32_t max_entries, uint64_t* cursor, uint64_t* dropped_out);

uint64_t head_snapshot();

}
