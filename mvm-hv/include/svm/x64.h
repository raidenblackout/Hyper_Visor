

#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct IA32_DESCRIPTOR
{
    uint16_t Limit;
    uint64_t Base;
};
static_assert(sizeof(IA32_DESCRIPTOR) == 10, "IA32_DESCRIPTOR size");

union IA32_SEGMENT_DESCRIPTOR
{
    struct
    {
        uint64_t LimitLow  : 16;
        uint64_t BaseLow   : 16;
        uint64_t BaseMid   : 8;
        uint64_t Type      : 4;
        uint64_t S         : 1;
        uint64_t DPL       : 2;
        uint64_t P         : 1;
        uint64_t LimitHigh : 4;
        uint64_t AVL       : 1;
        uint64_t L         : 1;
        uint64_t DB        : 1;
        uint64_t G         : 1;
        uint64_t BaseHigh  : 8;
    } Bits;
    uint64_t Uint64;
};
static_assert(sizeof(IA32_SEGMENT_DESCRIPTOR) == 8, "IA32_SEGMENT_DESCRIPTOR size");

union IA32_IDT_GATE_DESCRIPTOR
{
    struct
    {
        uint64_t OffsetLow   : 16;
        uint64_t Selector    : 16;
        uint64_t Ist         : 3;
        uint64_t Reserved1   : 5;
        uint64_t GateType    : 4;
        uint64_t System      : 1;
        uint64_t Dpl         : 2;
        uint64_t Present     : 1;
        uint64_t OffsetHigh  : 16;
        uint64_t OffsetUpper : 32;
        uint64_t Reserved2   : 32;
    } Bits;
    struct
    {
        uint64_t Low;
        uint64_t High;
    } Uint64;
};
static_assert(sizeof(IA32_IDT_GATE_DESCRIPTOR) == 16, "IA32_IDT_GATE_DESCRIPTOR size");

struct IA32_TASK_STATE_SEGMENT
{
    uint32_t Reserved0;
    uint64_t Rsp[3];
    uint64_t Reserved1;
    uint64_t Ist[7];
    uint64_t Reserved2;
    uint16_t Reserved3;
    uint16_t IoMapBase;
};
static_assert(sizeof(IA32_TASK_STATE_SEGMENT) == 104, "IA32_TASK_STATE_SEGMENT size");

#pragma pack(pop)
