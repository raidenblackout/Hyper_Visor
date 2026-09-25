#pragma once

#include "common.h"

struct HOST_EXCEPTION_STACK
{
    GUEST_REGISTERS GuestRegisters;
    uint64_t        InterruptNumber;
    uint64_t        ErrorCode;
    uint64_t        Rip;
    uint64_t        Cs;
    uint64_t        Rflags;
    uint64_t        Rsp;
    uint64_t        Ss;
};

extern "C" void HandleHostException(const HOST_EXCEPTION_STACK* stack);
