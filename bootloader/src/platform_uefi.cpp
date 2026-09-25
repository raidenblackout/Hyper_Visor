


#include <stdint.h>
#include <stdarg.h>
#include <intrin.h>

extern "C"
{
#include "efi/efi.h"
}

#include "hvmem/hvmem.h"
#include "hvmem/platform.h"


namespace
{
EFI_SYSTEM_TABLE* g_st = nullptr;


bool g_panicked = false;


void ascii_to_char16(CHAR16* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    while (src[i] && i + 1 < cap)
    {
        dst[i] = static_cast<CHAR16>(static_cast<unsigned char>(src[i]));
        ++i;
    }
    dst[i] = 0;
}


uint32_t append_uint64_hex(char* dst, uint32_t cap, uint32_t pos, uint64_t v)
{
    static const char hex[] = "0123456789abcdef";

    char     tmp[16];
    uint32_t n = 0;
    do
    {
        tmp[n++]   = hex[v & 0xF];
        v        >>= 4;
    } while (v && n < 16);
    while (n > 0 && pos + 1 < cap)
    {
        dst[pos++] = tmp[--n];
    }
    dst[pos] = 0;
    return pos;
}
uint32_t append_uint64_dec(char* dst, uint32_t cap, uint32_t pos, uint64_t v)
{
    char     tmp[20];
    uint32_t n = 0;
    do
    {
        tmp[n++]  = static_cast<char>('0' + (v % 10));
        v        /= 10;
    } while (v && n < 20);
    while (n > 0 && pos + 1 < cap)
    {
        dst[pos++] = tmp[--n];
    }
    dst[pos] = 0;
    return pos;
}
uint32_t append_str(char* dst, uint32_t cap, uint32_t pos, const char* s)
{
    while (*s && pos + 1 < cap)
        dst[pos++] = *s++;
    dst[pos] = 0;
    return pos;
}


uint32_t mini_format(char* out, uint32_t cap, const char* fmt, va_list ap)
{
    uint32_t pos = 0;
    while (*fmt && pos + 1 < cap)
    {
        if (*fmt != '%')
        {
            out[pos++] = *fmt++;
            continue;
        }
        ++fmt;

        bool long_long = false;
        if (fmt[0] == 'l' && fmt[1] == 'l')
        {
            long_long  = true;
            fmt       += 2;
        }
        switch (*fmt)
        {
        case 's':
            pos = append_str(out, cap, pos, va_arg(ap, const char*));
            ++fmt;
            break;
        case 'u':
            pos = append_uint64_dec(out, cap, pos, long_long ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t));
            ++fmt;
            break;
        case 'x':
        case 'X':
            pos = append_uint64_hex(out, cap, pos, long_long ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t));
            ++fmt;
            break;
        case 'p':
        {
            void* p = va_arg(ap, void*);
            if (pos + 3 < cap)
            {
                out[pos++] = '0';
                out[pos++] = 'x';
            }
            pos = append_uint64_hex(out, cap, pos, reinterpret_cast<uint64_t>(p));
            ++fmt;
            break;
        }
        case '%':
            if (pos + 1 < cap)
                out[pos++] = '%';
            ++fmt;
            break;
        default:
            if (pos + 1 < cap)
                out[pos++] = '?';
            ++fmt;
            break;
        }
    }
    out[pos] = 0;
    return pos;
}
} // namespace


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
    volatile long* w    = as_word(slot);
    unsigned       wait = 1;
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
    if (!g_st || !g_st->ConOut)
        return;

    va_list ap;
    va_start(ap, fmt);
    char ascii[256];

    uint32_t hdr = append_str(ascii, sizeof(ascii), 0, "[hvmem] ");
    mini_format(ascii + hdr, sizeof(ascii) - hdr, fmt, ap);
    va_end(ap);


    CHAR16 wide[260];
    ascii_to_char16(wide, 258, ascii);

    uint32_t n = 0;
    while (wide[n])
        ++n;
    if (n + 2 < 260)
    {
        wide[n++] = L'\r';
        wide[n++] = L'\n';
        wide[n]   = 0;
    }
    g_st->ConOut->OutputString(g_st->ConOut, wide);
}

[[noreturn]] void panic(const char* msg)
{
    g_panicked = true;
    log("PANIC: %s", msg ? msg : "(null)");

    for (;;)
    {
        __halt();
    }
}

} // namespace platform
} // namespace hvmem


extern "C" int hvmem_uefi_selftest(EFI_SYSTEM_TABLE* SystemTable,
                                   void*             heap_va,
                                   uint64_t          heap_pa,
                                   uint64_t          heap_size,
                                   char*             status_out,
                                   uint32_t          status_cap)
{
    g_st       = SystemTable;
    g_panicked = false;

    if (!hvmem::initialize(heap_va, heap_pa, heap_size))
    {
        if (status_out)
            append_str(status_out, status_cap, 0, "loader.efi: hvmem initialize FAILED");
        g_st = nullptr;
        return 0;
    }

    const bool ok = hvmem::selftest();

    if (status_out)
    {
        hvmem::stats s{};
        hvmem::get_stats(&s);
        uint32_t pos =
            append_str(status_out,
                       status_cap,
                       0,
                       ok ? "loader.efi: hvmem selftest OK size=" : "loader.efi: hvmem selftest FAILED size=");
        pos = append_uint64_hex(status_out, status_cap, pos, s.total_bytes);
        pos = append_str(status_out, status_cap, pos, " used=");
        pos = append_uint64_hex(status_out, status_cap, pos, s.used_bytes);
    }


    hvmem::shutdown();

    g_st = nullptr;
    return ok ? 1 : 0;
}
