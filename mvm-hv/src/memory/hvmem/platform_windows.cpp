

#define _NO_CRT_STDIO_INLINE

#include <ntddk.h>
#include <intrin.h>
#include <stdarg.h>
#include <ntstrsafe.h>

#include "hvmem/platform.h"
#include "log.h"
#include "hv_platform.h"
#include "boot_stubs.h"

namespace hvmem
{
namespace platform
{

static inline volatile long* as_word(void* slot)
{
    return static_cast<volatile long*>(slot);
}

void spin_init(void* slot)
{
    *as_word(slot) = 0;
}

void spin_acquire(void* slot)
{
    volatile long* w = as_word(slot);

    unsigned wait = 1;
    for (;;)
    {
        if (!*w && !_interlockedbittestandset(w, 0))
            return;
        for (unsigned i = 0; i < wait; ++i)
            _mm_pause();
        wait = (wait * 2 > 65536u) ? 65536u : wait * 2;
    }
}

void spin_release(void* slot)
{
    *as_word(slot) = 0;
}

void log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    if (hv_platform::is_boot_time())
    {
        (void)0;
        va_end(ap);
        return;
    }

    char buf[512];

    RtlStringCchVPrintfA(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LogPrint(LOG_TYPE_INFO, " [hvmem] %s", buf);
}

void panic(const char* msg)
{
    if (hv_platform::is_boot_time())
    {
        boot_stubs::halt(msg);
    }
    LogPrint(LOG_TYPE_ERROR, " [hvmem][PANIC] %s", msg ? msg : "(null)");

    KeBugCheckEx(0x000000E2, 0x484D454Dull, 0, 0, 0);
}

}
}
