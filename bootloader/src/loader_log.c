

#include "loader.h"
#include "../net/snp_send.h"

static const CHAR16 kLiveLogPath[] = L"\\EFI\\mvm\\hv-live.log";

EFI_FILE_PROTOCOL* OpenLiveLog(EFI_FILE_PROTOCOL* root)
{
    EFI_FILE_PROTOCOL* file = NULL;
    EFI_STATUS         s    = root->Open(
        root, &file, (CHAR16*)kLiveLogPath, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !file)
        return NULL;

    (void)file->SetPosition(file, 0xFFFFFFFFFFFFFFFFULL);
    static const char kBanner[] = "\r\n==== new boot session ====\r\n";
    UINTN             sz        = sizeof(kBanner) - 1;
    (void)file->Write(file, &sz, (VOID*)kBanner);
    (void)file->Flush(file);
    return file;
}

void LiveLogLine(EFI_FILE_PROTOCOL* file, const char* line)
{
    if (!file)
        return;
    char  buf[260];
    UINTN n = 0;
    while (line[n] && n < 256)
    {
        buf[n] = line[n];
        ++n;
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    UINTN sz = n;
    (void)file->Write(file, &sz, buf);
    (void)file->Flush(file);

    if (SnpSendIsArmed())
    {
        size_t n = 0;
        while (line[n])
            ++n;
        SnpSendUdpLine(line, n);
    }
}

void LogHvbBanner(EFI_FILE_PROTOCOL* file, void* hvb)
{
    if (!file)
        return;
    char  line[80];
    UINTN n = AsciiCat(line, sizeof(line), 0, "session hvb_pa=");
    char  hex[19];
    FormatHex64(hex, (UINT64)(UINTN)hvb);
    AsciiCat(line, sizeof(line), n, hex);
    LiveLogLine(file, line);
}

typedef EFI_STATUS(EFIAPI* EFI_LOCATE_HANDLE_BUFFER_t)(
    UINT32 SearchType, EFI_GUID* Protocol, VOID* SearchKey, UINTN* NoHandles, EFI_HANDLE** Buffer);

void LogLoadedImages(EFI_FILE_PROTOCOL* file)
{
    if (!file || !gBS)
        return;

    static const UINT64 kInterest[] = {
        0x785B5C38ULL, 0x785B8478ULL, 0x785B8330ULL, 0x6ABE3425ULL, 0x6AC563F5ULL, 0x77B10998ULL,

        0x785C4496ULL,

        0x785B6B90ULL, 0x785B83E0ULL, 0x785B8458ULL,
    };

    EFI_LOCATE_HANDLE_BUFFER_t locate = (EFI_LOCATE_HANDLE_BUFFER_t)gBS->LocateHandleBuffer;

    UINTN       count   = 0;
    EFI_HANDLE* handles = NULL;
    EFI_STATUS  s       = locate(2, &gLoadedImageProtocolGuid, NULL, &count, &handles);
    if (EFI_ERROR(s) || !handles || count == 0)
    {
        LiveLogLine(file, "images: LocateHandleBuffer failed or empty");
        return;
    }

    char  line[240];
    char  hex[19];
    UINTN n;

    n = AsciiCat(line, sizeof(line), 0, "images: count=");
    FormatHex64(hex, (UINT64)count);
    AsciiCat(line, sizeof(line), n, hex);
    LiveLogLine(file, line);

    for (UINTN i = 0; i < count; ++i)
    {
        EFI_LOADED_IMAGE_PROTOCOL* li = NULL;
        if (EFI_ERROR(gBS->HandleProtocol(handles[i], &gLoadedImageProtocolGuid, (VOID**)&li)) || !li)
        {
            continue;
        }

        const UINT64 base = (UINT64)(UINTN)li->ImageBase;
        const UINT64 size = li->ImageSize;

        int hit = 0;
        for (UINTN k = 0; k < sizeof(kInterest) / sizeof(kInterest[0]); ++k)
        {
            if (kInterest[k] >= base && kInterest[k] < base + size)
            {
                hit = 1;
                break;
            }
        }

        n = AsciiCat(line, sizeof(line), 0, hit ? "image *HIT* base=" : "image base=");
        FormatHex64(hex, base);
        n = AsciiCat(line, sizeof(line), n, hex);
        n = AsciiCat(line, sizeof(line), n, " end=");
        FormatHex64(hex, base + size);
        n = AsciiCat(line, sizeof(line), n, hex);

        if (hit)
        {
            for (UINTN k = 0; k < sizeof(kInterest) / sizeof(kInterest[0]); ++k)
            {
                if (kInterest[k] < base || kInterest[k] >= base + size)
                    continue;
                n = AsciiCat(line, sizeof(line), n, " @");
                FormatHex64(hex, kInterest[k]);
                n = AsciiCat(line, sizeof(line), n, hex);
                n = AsciiCat(line, sizeof(line), n, "(+");
                FormatHex64(hex, kInterest[k] - base);
                n = AsciiCat(line, sizeof(line), n, hex);
                n = AsciiCat(line, sizeof(line), n, ")");
            }

            n = AsciiCat(line, sizeof(line), n, " codetype=");
            FormatHex64(hex, (UINT64)li->ImageCodeType);
            n = AsciiCat(line, sizeof(line), n, hex);
        }

        LiveLogLine(file, line);

        if (hit && li->FilePath)
        {
            n = AsciiCat(line, sizeof(line), 0, "  dp=");
            n = AsciiCatHexBytes(line, sizeof(line), n, (const UINT8*)li->FilePath, 24);
            LiveLogLine(file, line);
        }
    }

    gBS->FreePool(handles);
}
