

#include "loader.h"

void Print(const CHAR16* s)
{
    if (gST && gST->ConOut)
        gST->ConOut->OutputString(gST->ConOut, (CHAR16*)s);
}

static const CHAR16 hexchars[] = L"0123456789ABCDEF";

void PrintHex64(UINT64 v)
{
    CHAR16 buf[19];
    buf[0] = L'0';
    buf[1] = L'x';
    for (int i = 0; i < 16; ++i)
        buf[2 + i] = hexchars[(v >> ((15 - i) * 4)) & 0xF];
    buf[18] = 0;
    Print(buf);
}

void FormatHex64(char* out, UINT64 v)
{
    static const char hex[] = "0123456789abcdef";
    out[0]                  = '0';
    out[1]                  = 'x';
    for (int i = 0; i < 16; ++i)
        out[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xF];
    out[18] = 0;
}

UINTN AsciiCat(char* dst, UINTN dst_cap, UINTN dst_len, const char* s)
{
    while (*s && dst_len + 1 < dst_cap)
        dst[dst_len++] = *s++;
    dst[dst_len] = 0;
    return dst_len;
}

UINTN AsciiCatHexBytes(char* dst, UINTN cap, UINTN len, const UINT8* src, UINTN count)
{
    static const char kHex[] = "0123456789abcdef";
    for (UINTN i = 0; i < count && len + 3 < cap; ++i)
    {
        dst[len++] = kHex[src[i] >> 4];
        dst[len++] = kHex[src[i] & 0xF];
        dst[len++] = ' ';
    }
    dst[len] = 0;
    return len;
}

UINTN Char16Len(const CHAR16* s)
{
    UINTN n = 0;
    while (s[n])
        ++n;
    return n;
}

UINT16 DpNodeLen(EFI_DEVICE_PATH_PROTOCOL* n)
{
    return (UINT16)(n->Length[0] | ((UINT16)n->Length[1] << 8));
}

BOOLEAN DpIsEnd(EFI_DEVICE_PATH_PROTOCOL* n)
{
    return (BOOLEAN)(n->Type == 0x7F && n->SubType == 0xFF);
}

UINTN DpBytesUntilEnd(EFI_DEVICE_PATH_PROTOCOL* dp)
{
    UINT8*                    p = (UINT8*)dp;
    EFI_DEVICE_PATH_PROTOCOL* n = dp;
    while (!DpIsEnd(n))
    {
        UINT16 l = DpNodeLen(n);
        if (l == 0)
            break;
        n = (EFI_DEVICE_PATH_PROTOCOL*)((UINT8*)n + l);
    }
    return (UINTN)((UINT8*)n - p);
}
