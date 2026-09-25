

#pragma once

#include <stdint.h>

namespace hvmem
{
namespace platform
{

inline constexpr uint32_t SPINLOCK_WORD_SIZE  = 8;
inline constexpr uint32_t SPINLOCK_WORD_ALIGN = 8;

void spin_init(void* slot);
void spin_acquire(void* slot);
void spin_release(void* slot);

void log(const char* fmt, ...);

[[noreturn]] void panic(const char* msg);

}
}
