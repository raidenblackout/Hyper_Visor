#pragma once

#include <ntddk.h>
#include <cstdint>

extern "C" bool pa_is_dram(uint64_t pa, uint64_t bytes);
