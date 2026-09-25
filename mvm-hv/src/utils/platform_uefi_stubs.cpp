

#include <intrin.h>
#include <stdarg.h>
#include <stdint.h>

#include "hv_platform.h"
#include "net/net_log.h"

extern "C"
{
#include "hvb.h"
}

extern "C" void SnpSendUdpLine(const char* line, size_t len);
extern "C" int  SnpSendIsArmed(void);

static constexpr unsigned short kCom1Base = 0x3F8;

static void serial_init_once()
{
    static bool inited = false;
    if (inited)
        return;
    inited = true;
    __outbyte(kCom1Base + 1, 0x00);
    __outbyte(kCom1Base + 3, 0x80);
    __outbyte(kCom1Base + 0, 0x01);
    __outbyte(kCom1Base + 1, 0x00);
    __outbyte(kCom1Base + 3, 0x03);
    __outbyte(kCom1Base + 2, 0xC7);
    __outbyte(kCom1Base + 4, 0x0B);
}

static void serial_write_char(char c)
{
    while ((__inbyte(kCom1Base + 5) & 0x20) == 0)
    {
    }
    __outbyte(kCom1Base, static_cast<unsigned char>(c));
}

static void serial_write_line(const char* s)
{
    serial_init_once();
    while (*s)
        serial_write_char(*s++);
    serial_write_char('\r');
    serial_write_char('\n');
}

extern "C"
{
    typedef struct EfiTextOutputStub EfiTextOutputStub;
    typedef unsigned long long       EfiStatus;
    typedef EfiStatus(__stdcall* EfiTextStringFn)(EfiTextOutputStub* self, unsigned short* string);
    struct EfiTextOutputStub
    {
        void*           Reset;
        EfiTextStringFn OutputString;
    };
    struct EfiSystemTableStub
    {
        unsigned char      header[24];
        unsigned short*    FirmwareVendor;
        unsigned int       FirmwareRevision;
        void*              ConsoleInHandle;
        void*              ConIn;
        void*              ConsoleOutHandle;
        EfiTextOutputStub* ConOut;
    };

    typedef struct EfiFileStub EfiFileStub;
    typedef EfiStatus(__stdcall* EfiFileWriteFn)(EfiFileStub* self, unsigned long long* buf_size, void* buffer);
    typedef EfiStatus(__stdcall* EfiFileFlushFn)(EfiFileStub* self);
    struct EfiFileStub
    {
        unsigned long long Revision;
        void*              Open;
        void*              Close;
        void*              Delete;
        void*              Read;
        EfiFileWriteFn     Write;
        void*              GetPosition;
        void*              SetPosition;
        void*              GetInfo;
        void*              SetInfo;
        EfiFileFlushFn     Flush;
    };
}

namespace boot_stubs
{

namespace
{
void append_str(char* dst, uint32_t cap, uint32_t& pos, const char* s)
{
    while (*s && pos + 1 < cap)
        dst[pos++] = *s++;
    dst[pos] = 0;
}

void append_pad(char* dst, uint32_t cap, uint32_t& pos, uint32_t n, uint32_t width, char pad)
{
    while (n < width && pos + 1 < cap)
    {
        dst[pos++] = pad;
        ++n;
    }
    dst[pos] = 0;
}
void append_hex(char* dst, uint32_t cap, uint32_t& pos, uint64_t v, uint32_t width = 0, char pad = ' ')
{
    static const char hex[] = "0123456789abcdef";
    char              tmp[16];
    uint32_t          n = 0;
    do
    {
        tmp[n++]   = hex[v & 0xF];
        v        >>= 4;
    } while (v && n < 16);
    append_pad(dst, cap, pos, n, width, pad);
    while (n > 0 && pos + 1 < cap)
        dst[pos++] = tmp[--n];
    dst[pos] = 0;
}
void append_dec(char* dst, uint32_t cap, uint32_t& pos, uint64_t v, uint32_t width = 0, char pad = ' ')
{
    char     tmp[20];
    uint32_t n = 0;
    do
    {
        tmp[n++]  = static_cast<char>('0' + (v % 10));
        v        /= 10;
    } while (v && n < 20);
    append_pad(dst, cap, pos, n, width, pad);
    while (n > 0 && pos + 1 < cap)
        dst[pos++] = tmp[--n];
    dst[pos] = 0;
}
}

uint32_t format_v(char* out, uint32_t cap, const char* fmt, va_list ap)
{
    uint32_t pos = 0;
    if (cap == 0)
        return 0;
    out[0] = 0;
    while (*fmt && pos + 1 < cap)
    {
        if (*fmt != '%')
        {
            out[pos++] = *fmt++;
            out[pos]   = 0;
            continue;
        }
        ++fmt;

        char     pad_char = ' ';
        uint32_t width    = 0;
        if (*fmt == '0')
        {
            pad_char = '0';
            ++fmt;
        }
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + static_cast<uint32_t>(*fmt - '0');
            ++fmt;
        }
        if (width > 32)
            width = 32;
        bool long_long = false;
        if (fmt[0] == 'l' && fmt[1] == 'l')
        {
            long_long  = true;
            fmt       += 2;
        }
        else if (fmt[0] == 'l')
        {
            long_long = true;
            ++fmt;
        }
        switch (*fmt)
        {
        case 's':
            append_str(out, cap, pos, va_arg(ap, const char*));
            ++fmt;
            break;
        case 'u':
            append_dec(
                out, cap, pos, long_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t), width, pad_char);
            ++fmt;
            break;
        case 'd':
            append_dec(out,
                       cap,
                       pos,
                       long_long ? (uint64_t)va_arg(ap, int64_t) : (uint64_t)va_arg(ap, int32_t),
                       width,
                       pad_char);
            ++fmt;
            break;
        case 'x':
        case 'X':
            append_hex(
                out, cap, pos, long_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t), width, pad_char);
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
            append_hex(out, cap, pos, reinterpret_cast<uint64_t>(p));
            ++fmt;
            break;
        }
        case 'c':
            if (pos + 1 < cap)
                out[pos++] = static_cast<char>(va_arg(ap, int));
            ++fmt;
            break;
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
        out[pos] = 0;
    }
    return pos;
}

static void console_write_line(const char* s)
{
    auto* st = static_cast<EfiSystemTableStub*>(hv_platform::system_table());
    if (!st || !st->ConOut || !st->ConOut->OutputString)
        return;

    unsigned short wide[260];
    unsigned       n = 0;
    while (s[n] && n < 256)
    {
        wide[n] = (unsigned short)(unsigned char)s[n];
        ++n;
    }
    wide[n++] = '\r';
    wide[n++] = '\n';
    wide[n]   = 0;
    st->ConOut->OutputString(st->ConOut, wide);
}

static void live_log_write_line(const char* s)
{
    auto* file = static_cast<EfiFileStub*>(hv_platform::debug_log_file());
    if (!file || !file->Write)
        return;

    char     buf[260];
    unsigned n = 0;
    while (s[n] && n < 256)
    {
        buf[n] = s[n];
        ++n;
    }
    buf[n++] = '\r';
    buf[n++] = '\n';

    unsigned long long size = n;
    (void)file->Write(file, &size, buf);
    if (file->Flush)
        (void)file->Flush(file);
}

static volatile bool s_firmware_sinks = true;

void set_firmware_sinks(bool enabled)
{
    s_firmware_sinks = enabled;
}
bool firmware_sinks_enabled()
{
    return s_firmware_sinks;
}

static volatile bool s_log_mem_enabled = true;

void set_log_mem_enabled(bool on)
{
    s_log_mem_enabled = on;
}
bool log_mem_enabled()
{
    return s_log_mem_enabled;
}

void log_mem(const char* fmt, ...)
{
    if (!s_log_mem_enabled)
        return;

    char    buf[256];
    va_list ap;
    va_start(ap, fmt);
    format_v(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    size_t n = 0;
    while (n < sizeof(buf) && buf[n])
        ++n;
    net_log::enqueue(buf, n);
}

void log_prefix_v(const char* prefix, const char* fmt, va_list ap)
{
    if (!s_log_mem_enabled)
        return;

    char   buf[256];
    size_t off = 0;
    while (off + 1 < sizeof(buf) && prefix && prefix[off])
    {
        buf[off] = prefix[off];
        ++off;
    }
    format_v(buf + off, static_cast<uint32_t>(sizeof(buf) - off), fmt, ap);

    size_t n = 0;
    while (n < sizeof(buf) && buf[n])
        ++n;
    net_log::enqueue(buf, n);
}

void log_v(const char* fmt, va_list ap)
{
    char buf[256];
    format_v(buf, sizeof(buf), fmt, ap);

    if (s_firmware_sinks)
    {
        console_write_line(buf);

        live_log_write_line(buf);

        if (SnpSendIsArmed())
        {
            size_t n = 0;
            while (buf[n])
                ++n;
            SnpSendUdpLine(buf, n);
        }
    }

    serial_write_line(buf);
}

void log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(fmt, ap);
    va_end(ap);
}

#ifndef HVB_LOG_MEM_SERIAL
#define HVB_LOG_MEM_SERIAL 0
#endif
#ifndef HVB_LOG_MEM_SERIAL_CAP
#define HVB_LOG_MEM_SERIAL_CAP 128
#endif

[[noreturn]] void halt(const char* msg)
{
    if (msg)
        log("[boot_panic] %s", msg);
    _disable();
    for (;;)
    {
        __halt();
    }
}
}
