

#include "hv_toggles.h"

#if HV_FB_LOG_ENABLE

#include <intrin.h>
#include <stdarg.h>
#include <stdint.h>

#include "fb_panic.h"
#include "svm/paging.h"

extern "C"
{
#include "hvb.h"
}

namespace boot_stubs
{
uint32_t format_v(char* out, uint32_t cap, const char* fmt, va_list ap);
}

namespace fb_panic
{
namespace
{

constexpr char kGlyphChars[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:=-._/()?!*#+,<>[]";

const uint8_t kFont[][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00 },
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F, 0x00 }, { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00 },
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00 }, { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00 },
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00 }, { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00 },
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00 }, { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00 },
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C, 0x00 }, { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00 },
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00 }, { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00 },
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00 }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00 },
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00 }, { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E, 0x00 },
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00 }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F, 0x00 },
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00 }, { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00 }, { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00 },
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, 0x00 }, { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00 },
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00 }, { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00 },
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00 }, { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00 },
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 }, { 0x11, 0x11, 0x11, 0x11, 0x15, 0x1B, 0x11, 0x00 },
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00 }, { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00 },
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00 }, { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00 },
    { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00 },
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10, 0x00 }, { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00 },
    { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00 }, { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00 },
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 }, { 0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00, 0x00 },
    { 0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00, 0x00 }, { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08, 0x00 }, { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00 },
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00 }, { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00 },
    { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00 },
};

static_assert(sizeof(kGlyphChars) - 1 == sizeof(kFont) / sizeof(kFont[0]), "kGlyphChars and kFont are out of step");

constexpr uint32_t kGlyphW = 5;
constexpr uint32_t kGlyphH = 7;
constexpr uint32_t kCellW  = 6;
constexpr uint32_t kCellH  = 8;
constexpr uint32_t kScale  = 2;

constexpr uint32_t kMaxRows         = 40;
constexpr uint32_t kMaxBandFraction = 2;
constexpr uint32_t kMaxTrackedRows  = 256;
constexpr uint32_t kMaxStatusSlots  = 64;
constexpr uint32_t kLineChars       = 224;

constexpr uint32_t kInk   = 0xFFFFFFFFu;
constexpr uint32_t kPaper = 0x00000000u;

volatile uint32_t* s_fb         = nullptr;
uint32_t           s_pixels     = 0;
uint32_t           s_stride     = 0;
uint32_t           s_width      = 0;
uint32_t           s_rows       = 0;
uint32_t           s_total_rows = 0;
const char*        s_status     = "fb_panic: init() not called";

volatile long s_next_row = 0;

volatile bool s_enabled = true;

uint16_t s_row_chars[kMaxTrackedRows]             = {};
char     s_slot_text[kMaxStatusSlots][kLineChars] = {};
char     s_report_text[kMaxRows][kLineChars]      = {};
uint32_t s_report_used                            = 0;

uint32_t s_watch_x = 0;
uint32_t s_watch_y = 0;

volatile long s_row_lock[kMaxTrackedRows] = {};

bool row_trylock(uint32_t row)
{
    return row < kMaxTrackedRows && _InterlockedCompareExchange(&s_row_lock[row], 1, 0) == 0;
}
void row_unlock(uint32_t row)
{
    if (row < kMaxTrackedRows)
        _InterlockedExchange(&s_row_lock[row], 0);
}

constexpr uint32_t kUnknownGlyph = 45;
static_assert(kGlyphChars[kUnknownGlyph] == '?', "kUnknownGlyph no longer indexes '?'");

uint32_t glyph_index(char c)
{
    if (c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
    for (uint32_t i = 0; i < sizeof(kGlyphChars) - 1; ++i)
        if (kGlyphChars[i] == c)
            return i;
    return kUnknownGlyph;
}

inline void put(uint32_t x, uint32_t y, uint32_t color)
{
    const uint32_t idx = y * s_stride + x;
    if (idx < s_pixels)
        s_fb[idx] = color;
}

void fill_band(uint32_t y0, uint32_t h, uint32_t x0, uint32_t x1, uint32_t color)
{
    if (x1 > s_width)
        x1 = s_width;
    for (uint32_t y = y0; y < y0 + h; ++y)
        for (uint32_t x = x0; x < x1; ++x)
            put(x, y, color);
}

void draw_glyph(uint32_t gi, uint32_t x0, uint32_t y0)
{
    const uint8_t* rows = kFont[gi];
    for (uint32_t gy = 0; gy < kGlyphH; ++gy)
    {
        const uint8_t bits = rows[gy];
        if (!bits)
            continue;
        for (uint32_t gx = 0; gx < kGlyphW; ++gx)
        {
            if (!(bits & (0x10u >> gx)))
                continue;
            for (uint32_t sy = 0; sy < kScale; ++sy)
                for (uint32_t sx = 0; sx < kScale; ++sx)
                    put(x0 + gx * kScale + sx, y0 + gy * kScale + sy, kInk);
        }
    }
}

void paint_row(uint32_t row, const char* text, bool flush, bool full_width)
{
    const uint32_t y0 = row * kCellH * kScale;

    const uint32_t max_cols = s_width / (kCellW * kScale);
    uint32_t       cols     = 0;
    while (cols < max_cols && text[cols])
        ++cols;

    const uint32_t prev_cols  = (row < kMaxTrackedRows) ? s_row_chars[row] : max_cols;
    const uint32_t clear_cols = full_width ? max_cols : ((cols > prev_cols) ? cols : prev_cols);

    fill_band(y0, kCellH * kScale, 0, clear_cols * kCellW * kScale, kPaper);

    for (uint32_t col = 0; col < cols; ++col)
        draw_glyph(glyph_index(text[col]), col * kCellW * kScale, y0);

    if (row < kMaxTrackedRows)
        s_row_chars[row] = static_cast<uint16_t>(cols);

    _mm_sfence();
    if (flush)
        __wbinvd();
}

void stamp_watch()
{
    put(s_watch_x, s_watch_y, kInk);
    _mm_sfence();
}

bool watch_intact()
{
    const uint32_t idx = s_watch_y * s_stride + s_watch_x;
    return idx < s_pixels && s_fb[idx] == kInk;
}
}

void init(const void* hvb_header)
{
    s_fb     = nullptr;
    s_pixels = 0;
    s_rows   = 0;

    if (!hvb_header)
    {
        s_status = "fb_panic: no HVB header";
        return;
    }

    auto* h = static_cast<const hvb_header_t*>(hvb_header);
    if (h->magic != HVB_MAGIC)
    {
        s_status = "fb_panic: HVB magic mismatch";
        return;
    }
    if (h->fb_base == 0 || h->fb_size == 0)
    {
        s_status = "fb_panic: loader published no framebuffer";
        return;
    }
    if (h->fb_stride == 0 || h->fb_width == 0 || h->fb_height == 0)
    {
        s_status = "fb_panic: degenerate framebuffer geometry";
        return;
    }

    const uint64_t kHostMapLimit = static_cast<uint64_t>(PAGING_PML4_COUNT) * 512ULL * 1024ULL * 1024ULL * 1024ULL;
    if (h->fb_base + h->fb_size > kHostMapLimit)
    {
        s_status = "fb_panic: framebuffer above host identity map limit";
        return;
    }

    const uint64_t implied = static_cast<uint64_t>(h->fb_stride) * h->fb_height * sizeof(uint32_t);
    const uint64_t usable  = (implied < h->fb_size) ? implied : h->fb_size;

    s_stride = h->fb_stride;
    s_width  = h->fb_width;
    s_pixels = static_cast<uint32_t>(usable / sizeof(uint32_t));

    uint32_t rows = h->fb_height / (kCellH * kScale);
    if (rows > kMaxTrackedRows)
        rows = kMaxTrackedRows;
    s_total_rows = rows;

    uint32_t band = rows / kMaxBandFraction;
    if (band == 0 && rows > 0)
        band = rows;
    s_rows = (band > kMaxRows) ? kMaxRows : band;

    for (uint32_t i = 0; i < kMaxTrackedRows; ++i)
        s_row_chars[i] = 0;
    for (uint32_t i = 0; i < kMaxStatusSlots; ++i)
        s_slot_text[i][0] = 0;
    for (uint32_t i = 0; i < kMaxRows; ++i)
        s_report_text[i][0] = 0;
    s_report_used = 0;

    s_watch_x = s_width - 1;
    s_watch_y = 0;

    if (s_rows == 0 || s_pixels == 0)
    {
        s_status = "fb_panic: framebuffer too small for any text row";
        return;
    }

    s_fb       = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(h->fb_base));
    s_next_row = 0;
    s_status   = "fb_panic: armed";
}

bool available()
{
    return s_fb != nullptr;
}

const char* status()
{
    return s_status;
}

bool is_enabled()
{
    return s_enabled;
}
void set_enabled(bool on)
{
    s_enabled = on;
}

void report(const char* line)
{
    if (!s_enabled || !s_fb || !line)
        return;

    const long row = _InterlockedIncrement(&s_next_row) - 1;
    if (row < 0 || static_cast<uint32_t>(row) >= s_rows)
        return;

    {
        char*    dst = s_report_text[row];
        uint32_t i   = 0;
        for (; i + 1 < kLineChars && line[i]; ++i)
            dst[i] = line[i];
        dst[i] = 0;
        if (static_cast<uint32_t>(row) + 1 > s_report_used)
            s_report_used = static_cast<uint32_t>(row) + 1;
    }

    paint_row(static_cast<uint32_t>(row), line, true, true);
    stamp_watch();
}

void reportf(const char* fmt, ...)
{
    if (!s_enabled || !s_fb)
        return;

    char    buf[kLineChars];
    va_list ap;
    va_start(ap, fmt);
    boot_stubs::format_v(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    report(buf);
}

namespace
{
bool slot_to_row(uint32_t slot, uint32_t& row_out)
{
    if (slot + 1 > s_total_rows)
        return false;
    const uint32_t row = s_total_rows - 1 - slot;
    if (row < s_rows)
        return false;
    row_out = row;
    return true;
}
}

void markf(uint32_t slot, const char* fmt, ...)
{
    uint32_t row;
    if (!s_enabled || !s_fb || !slot_to_row(slot, row))
        return;

    char    buf[kLineChars];
    va_list ap;
    va_start(ap, fmt);
    boot_stubs::format_v(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    paint_row(row, buf, true, true);

    if (slot < kMaxStatusSlots)
    {
        char*    shadow = s_slot_text[slot];
        uint32_t i      = 0;
        for (; i + 1 < kLineChars && buf[i]; ++i)
            shadow[i] = buf[i];
        shadow[i] = 0;
    }
}

void statusf(uint32_t slot, const char* fmt, ...)
{
    uint32_t row;
    if (!s_enabled || !s_fb || slot >= kMaxStatusSlots || !slot_to_row(slot, row))
        return;

    char    buf[kLineChars];
    va_list ap;
    va_start(ap, fmt);
    boot_stubs::format_v(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    char* shadow = s_slot_text[slot];
    bool  same   = true;
    for (uint32_t i = 0; i < kLineChars; ++i)
    {
        if (shadow[i] != buf[i])
        {
            same = false;
            break;
        }
        if (buf[i] == 0)
            break;
    }
    if (same)
        return;

    if (!row_trylock(row))
        return;

    paint_row(row, buf, false, false);

    uint32_t i = 0;
    for (; i + 1 < kLineChars && buf[i]; ++i)
        shadow[i] = buf[i];
    shadow[i] = 0;

    row_unlock(row);
}

void restore_if_wiped()
{
    if (!s_enabled || !s_fb)
        return;

    static volatile long s_restores_left = 8;
    if (s_restores_left <= 0)
        return;

    if (watch_intact())
        return;

    static volatile long s_restore_lock = 0;
    if (_InterlockedCompareExchange(&s_restore_lock, 1, 0) != 0)
        return;

    _InterlockedDecrement(&s_restores_left);

    for (uint32_t row = 0; row < s_report_used && row < kMaxRows; ++row)
    {
        if (!s_report_text[row][0])
            continue;
        if (!row_trylock(row))
            continue;
        paint_row(row, s_report_text[row], false, true);
        row_unlock(row);
    }

    for (uint32_t slot = 0; slot < kMaxStatusSlots; ++slot)
    {
        uint32_t row;
        if (!s_slot_text[slot][0] || !slot_to_row(slot, row))
            continue;
        if (!row_trylock(row))
            continue;
        paint_row(row, s_slot_text[slot], false, true);
        row_unlock(row);
    }

    stamp_watch();
    _InterlockedExchange(&s_restore_lock, 0);
}
}

#endif
