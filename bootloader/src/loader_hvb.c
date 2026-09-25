

#include "loader.h"

const CHAR16* g_hvb_strategy = L"none";

static EFI_STATUS TryReserveAt(EFI_PHYSICAL_ADDRESS pa, UINTN pages, EFI_PHYSICAL_ADDRESS* out)
{
    EFI_PHYSICAL_ADDRESS addr = pa;
    EFI_STATUS           s    = gBS->AllocatePages(AllocateAddress, EfiReservedMemoryType, pages, &addr);
    if (EFI_ERROR(s))
        return s;
    *out = addr;
    return EFI_SUCCESS;
}

EFI_STATUS ReserveHvb(EFI_PHYSICAL_ADDRESS* out_pa)
{
    UINTN                pages = EFI_SIZE_TO_PAGES(HVB_TOTAL_SIZE);
    EFI_PHYSICAL_ADDRESS addr  = 0;

    if (!EFI_ERROR(TryReserveAt(0x100000000ULL, pages, &addr)))
    {
        g_hvb_strategy = L"fixed@4GB";
        goto done;
    }

    if (!EFI_ERROR(TryReserveAt(0x80000000ULL, pages, &addr)))
    {
        g_hvb_strategy = L"fixed@2GB";
        goto done;
    }

    addr = 0x400000000ULL;
    if (!EFI_ERROR(gBS->AllocatePages(AllocateMaxAddress, EfiReservedMemoryType, pages, &addr)))
    {
        g_hvb_strategy = L"max<16GB";
        goto done;
    }

    addr         = 0;
    EFI_STATUS s = gBS->AllocatePages(AllocateAnyPages, EfiReservedMemoryType, pages, &addr);
    if (EFI_ERROR(s))
        return s;
    g_hvb_strategy = L"any";

done:

    gBS->SetMem((VOID*)(UINTN)addr, HVB_TOTAL_SIZE, 0);
    *out_pa = addr;
    return EFI_SUCCESS;
}

EFI_STATUS WriteHvbHint(EFI_FILE_PROTOCOL* root, UINT64 hvb_pa)
{
    static const CHAR16 kHintPath[] = L"\\EFI\\mvm\\hvb_addr.txt";
    char                buf[20];
    FormatHex64(buf, hvb_pa);
    buf[18] = '\r';
    buf[19] = '\n';

    EFI_FILE_PROTOCOL* file = NULL;
    EFI_STATUS         s =
        root->Open(root, &file, (CHAR16*)kHintPath, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !file)
        return s ? s : EFI_LOAD_ERROR;

    (void)file->SetPosition(file, 0);

    UINTN size = sizeof(buf);
    s          = file->Write(file, &size, buf);
    file->Close(file);
    if (EFI_ERROR(s) || size != sizeof(buf))
        return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}

void FillHvbHeader(void* hvb_base, EFI_PHYSICAL_ADDRESS hvb_pa)
{
    hvb_header_t* h        = (hvb_header_t*)hvb_base;
    h->magic               = HVB_MAGIC;
    h->version             = HVB_VERSION;
    h->total_size          = HVB_TOTAL_SIZE;
    h->hvb_physical        = (hvb_u64)hvb_pa;
    h->image_region_offset = HVB_IMAGE_OFFSET;
    h->image_region_size   = HVB_IMAGE_SIZE;
    h->heap_offset         = HVB_HEAP_OFFSET;
    h->heap_size           = HVB_HEAP_SIZE;
    h->entry_rva           = 0;
    h->flags               = 0;
    h->loader_build        = 20260816;
    h->windows_build       = 26100;

    h->fb_base   = 0;
    h->fb_size   = 0;
    h->fb_width  = 0;
    h->fb_height = 0;
    h->fb_stride = 0;
    h->fb_format = 0;
}

void QueryGopIntoHvb(void* hvb, EFI_FILE_PROTOCOL* diag)
{
    hvb_header_t* h = (hvb_header_t*)hvb;

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_STATUS                    s   = gBS->LocateProtocol(&gGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);
    if (EFI_ERROR(s) || !gop || !gop->Mode || !gop->Mode->Info)
    {
        Print(L"  GOP unavailable; on-screen panic channel disabled\r\n");
        LiveLogLine(diag, "gop: LocateProtocol FAILED -- fb_panic disabled");
        return;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mi = gop->Mode->Info;

    if (mi->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
        mi->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
    {
        Print(L"  GOP pixel format unsupported (");
        PrintHex64((UINT64)mi->PixelFormat);
        Print(L"); on-screen panic channel disabled\r\n");
        LiveLogLine(diag, "gop: unsupported PixelFormat -- fb_panic disabled");
        return;
    }

    h->fb_base   = (hvb_u64)gop->Mode->FrameBufferBase;
    h->fb_size   = (hvb_u64)gop->Mode->FrameBufferSize;
    h->fb_width  = (hvb_u32)mi->HorizontalResolution;
    h->fb_height = (hvb_u32)mi->VerticalResolution;
    h->fb_stride = (hvb_u32)mi->PixelsPerScanLine;
    h->fb_format = (hvb_u32)mi->PixelFormat;

    Print(L"  GOP fb_base=");
    PrintHex64((UINT64)h->fb_base);
    Print(L" size=");
    PrintHex64((UINT64)h->fb_size);
    Print(L" stride=");
    PrintHex64((UINT64)h->fb_stride);
    Print(L"\r\n");

    {
        char  line[200];
        char  hex[19];
        UINTN n = AsciiCat(line, sizeof(line), 0, "gop: fb_base=");
        FormatHex64(hex, (UINT64)h->fb_base);
        n = AsciiCat(line, sizeof(line), n, hex);
        n = AsciiCat(line, sizeof(line), n, " fb_size=");
        FormatHex64(hex, (UINT64)h->fb_size);
        n = AsciiCat(line, sizeof(line), n, hex);
        n = AsciiCat(line, sizeof(line), n, " width=");
        FormatHex64(hex, (UINT64)h->fb_width);
        n = AsciiCat(line, sizeof(line), n, hex);
        n = AsciiCat(line, sizeof(line), n, " height=");
        FormatHex64(hex, (UINT64)h->fb_height);
        n = AsciiCat(line, sizeof(line), n, hex);
        n = AsciiCat(line, sizeof(line), n, " stride=");
        FormatHex64(hex, (UINT64)h->fb_stride);
        n = AsciiCat(line, sizeof(line), n, hex);
        n = AsciiCat(line, sizeof(line), n, " fmt=");
        FormatHex64(hex, (UINT64)h->fb_format);
        AsciiCat(line, sizeof(line), n, hex);
        LiveLogLine(diag, line);
    }
}

UINT64 ParseHexPa(const char* s, UINTN n)
{
    if (n < 18 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
        return 0;
    UINT64 v = 0;
    for (int i = 2; i < 18; ++i)
    {
        char   c = s[i];
        UINT64 d;
        if (c >= '0' && c <= '9')
            d = (UINT64)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (UINT64)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (UINT64)(c - 'A' + 10);
        else
            return 0;
        v = (v << 4) | d;
    }
    return v;
}

void ReadFeaturesCfg(EFI_FILE_PROTOCOL* root, EFI_FILE_PROTOCOL* log, hv_features_v1* out)
{
    HvFeaturesDefaults(out);
    if (!root)
        return;

    void*      cfg_raw  = NULL;
    UINTN      cfg_size = 0;
    EFI_STATUS s        = ReadFile(root, L"\\EFI\\mvm\\hv_features.cfg", &cfg_raw, &cfg_size);
    if (EFI_ERROR(s) || !cfg_raw || cfg_size == 0)
    {
        if (log)
            LiveLogLine(log, "hv_features_cfg: no hv_features.cfg on ESP -- using defaults");
        return;
    }

    char err[128] = { 0 };
    int  rc       = HvFeaturesParse((const char*)cfg_raw, (size_t)cfg_size, out, err, sizeof(err));
    gBS->FreePool(cfg_raw);
    if (rc != 0 && log)
    {
        LiveLogLine(log, "hv_features_cfg: parse FAILED -- defaults retained");
    }
}

void FillHvbFeaturesCfg(void* hvb, EFI_FILE_PROTOCOL* root, EFI_FILE_PROTOCOL* log)
{
    hvb_header_t* hdr = (hvb_header_t*)hvb;
    ReadFeaturesCfg(root, log, &hdr->features);

    char  line[192];
    char  num[4];
    UINTN n = AsciiCat(line, sizeof(line), 0, "hv_features_cfg:");
#define APPEND_BOOL(name, field)                                \
    do                                                          \
    {                                                           \
        n      = AsciiCat(line, sizeof(line), n, " " name "="); \
        num[0] = '0' + (char)(hdr->features.field ? 1 : 0);     \
        num[1] = '\0';                                          \
        n      = AsciiCat(line, sizeof(line), n, num);          \
    } while (0)
    APPEND_BOOL("virt", virtualize);
    APPEND_BOOL("fb", fb_log);
    APPEND_BOOL("log", log_mem);
    APPEND_BOOL("shdn", intercept_shutdown);
    APPEND_BOOL("pause", intercept_pause);
    APPEND_BOOL("guard", intercept_svm_guard);
    APPEND_BOOL("msrpr", intercept_msr_prot);
#undef APPEND_BOOL
    n = AsciiCat(line, sizeof(line), n, " menu_ms=");
    {
        uint32_t v = hdr->features.boot_menu_timeout_ms;
        char     tmp[11];
        int      ti = 0;
        if (v == 0)
            tmp[ti++] = '0';
        else
            while (v)
            {
                tmp[ti++]  = (char)('0' + (v % 10u));
                v         /= 10u;
            }
        while (ti > 0)
        {
            num[0] = tmp[--ti];
            num[1] = '\0';
            n      = AsciiCat(line, sizeof(line), n, num);
        }
    }
    LiveLogLine(log, line);
}

void FillHvbNetlogCfg(void* hvb, EFI_FILE_PROTOCOL* root, EFI_FILE_PROTOCOL* log)
{
    hvb_header_t* hdr = (hvb_header_t*)hvb;

    void*      cfg_raw  = NULL;
    UINTN      cfg_size = 0;
    EFI_STATUS s        = ReadFile(root, L"\\EFI\\mvm\\netlog.cfg", &cfg_raw, &cfg_size);
    if (EFI_ERROR(s) || !cfg_raw || cfg_size == 0)
    {
        LiveLogLine(log, "netlog_cfg: no netlog.cfg on ESP -- disabled");

        return;
    }

    char err[128] = { 0 };
    int  rc       = NetlogCfgParse((const char*)cfg_raw, (size_t)cfg_size, &hdr->netlog, err, sizeof(err));
    gBS->FreePool(cfg_raw);

    if (rc != 0)
    {
        char  line[192];
        UINTN n = AsciiCat(line, sizeof(line), 0, "netlog_cfg: parse FAILED (");
        n       = AsciiCat(line, sizeof(line), n, err);
        AsciiCat(line, sizeof(line), n, ") -- disabled");
        LiveLogLine(log, line);

        return;
    }

    char bdf1[9], bdf2[9];
    NetlogCfgFormatBdf(hdr->netlog.snp_bdf, bdf1);
    NetlogCfgFormatBdf(hdr->netlog.hv_bdf, bdf2);

    char  line[192];
    char  num[16];
    UINTN n = AsciiCat(line, sizeof(line), 0, "netlog_cfg: enable=");
    num[0]  = '0' + (char)hdr->netlog.enable;
    num[1]  = '\0';
    n       = AsciiCat(line, sizeof(line), n, num);

    n      = AsciiCat(line, sizeof(line), n, " mode=");
    num[0] = '0' + (char)hdr->netlog.mode;
    num[1] = '\0';
    n      = AsciiCat(line, sizeof(line), n, num);

    n = AsciiCat(line, sizeof(line), n, " dest=");
    FormatHex64(num, hdr->netlog.dest_ip_be);
    n = AsciiCat(line, sizeof(line), n, num);

    n             = AsciiCat(line, sizeof(line), n, ":");
    uint16_t port = (uint16_t)((hdr->netlog.dest_port_be << 8) | (hdr->netlog.dest_port_be >> 8));
    int      pidx = 0;
    char     pbuf[8];
    if (port == 0)
        pbuf[pidx++] = '0';
    else
    {
        uint16_t t = port;
        while (t)
        {
            pbuf[pidx++]  = '0' + (t % 10);
            t            /= 10;
        }
    }
    while (pidx > 0)
    {
        num[0] = pbuf[--pidx];
        num[1] = '\0';
        n      = AsciiCat(line, sizeof(line), n, num);
    }

    n = AsciiCat(line, sizeof(line), n, " snp_bdf=");
    n = AsciiCat(line, sizeof(line), n, bdf1);
    n = AsciiCat(line, sizeof(line), n, " hv_bdf=");
    AsciiCat(line, sizeof(line), n, bdf2);

    LiveLogLine(log, line);
}

static const EFI_GUID gEfiAcpi20TableGuid = {
    0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};

#pragma pack(push, 1)
typedef struct acpi_rsdp_v2
{
    char     sig[8];
    uint8_t  csum;
    char     oemid[6];
    uint8_t  rev;
    uint32_t rsdt;
    uint32_t len;
    uint64_t xsdt;
    uint8_t  xcsum;
    uint8_t  rsvd[3];
} acpi_rsdp_v2_t;
typedef struct acpi_sdt_hdr
{
    char     sig[4];
    uint32_t len;
    uint8_t  rev;
    uint8_t  csum;
    char     oemid[6];
    char     oemtblid[8];
    uint32_t oemrev;
    uint32_t creatorid;
    uint32_t creatorrev;
} acpi_sdt_hdr_t;
typedef struct mcfg_alloc
{
    uint64_t base;
    uint16_t seg;
    uint8_t  bus_start;
    uint8_t  bus_end;
    uint32_t reserved;
} mcfg_alloc_t;
#pragma pack(pop)

void FillHvbMcfgBase(void* hvb, EFI_SYSTEM_TABLE* st, EFI_FILE_PROTOCOL* log)
{
    hvb_header_t* hdr = (hvb_header_t*)hvb;
    if (!hdr->netlog.enable)
        return;

    acpi_rsdp_v2_t* rsdp = NULL;
    for (UINTN i = 0; i < st->NumberOfTableEntries; ++i)
    {
        EFI_GUID* g = &st->ConfigurationTable[i].VendorGuid;
        if (g->Data1 == gEfiAcpi20TableGuid.Data1 && g->Data2 == gEfiAcpi20TableGuid.Data2 &&
            g->Data3 == gEfiAcpi20TableGuid.Data3 && *(uint64_t*)g->Data4 == *(uint64_t*)gEfiAcpi20TableGuid.Data4)
        {
            rsdp = (acpi_rsdp_v2_t*)st->ConfigurationTable[i].VendorTable;
            break;
        }
    }
    if (!rsdp)
    {
        LiveLogLine(log, "netlog_cfg: no ACPI 2.0 RSDP -- MCFG hiding disabled");
        hdr->netlog.enable = 0;
        return;
    }

    acpi_sdt_hdr_t* xsdt = (acpi_sdt_hdr_t*)(UINTN)rsdp->xsdt;
    if (!xsdt || xsdt->sig[0] != 'X' || xsdt->sig[1] != 'S' || xsdt->sig[2] != 'D' || xsdt->sig[3] != 'T')
    {
        LiveLogLine(log, "netlog_cfg: bad XSDT -- MCFG hiding disabled");
        hdr->netlog.enable = 0;
        return;
    }

    uint32_t  num = (xsdt->len - sizeof(*xsdt)) / 8;
    uint64_t* tbl = (uint64_t*)((uint8_t*)xsdt + sizeof(*xsdt));
    for (uint32_t i = 0; i < num; ++i)
    {
        acpi_sdt_hdr_t* e = (acpi_sdt_hdr_t*)(UINTN)tbl[i];
        if (!e)
            continue;
        if (e->sig[0] == 'M' && e->sig[1] == 'C' && e->sig[2] == 'F' && e->sig[3] == 'G')
        {
            mcfg_alloc_t* a          = (mcfg_alloc_t*)((uint8_t*)e + sizeof(*e) + 8);
            hdr->netlog.mcfg_base_pa = a->base;

            char  line[128];
            char  num_str[32];
            UINTN n = AsciiCat(line, sizeof(line), 0, "netlog_cfg: mcfg_base=");
            FormatHex64(num_str, a->base);
            n = AsciiCat(line, sizeof(line), n, num_str);
            n = AsciiCat(line, sizeof(line), n, " bus_range=");

            int  pidx = 0;
            char pbuf[8];
            if (a->bus_start == 0)
                pbuf[pidx++] = '0';
            else
            {
                uint8_t t = a->bus_start;
                while (t)
                {
                    pbuf[pidx++]  = '0' + (t % 10);
                    t            /= 10;
                }
            }
            while (pidx > 0)
            {
                num_str[0] = pbuf[--pidx];
                num_str[1] = '\0';
                n          = AsciiCat(line, sizeof(line), n, num_str);
            }

            n = AsciiCat(line, sizeof(line), n, "..");

            pidx = 0;
            if (a->bus_end == 0)
                pbuf[pidx++] = '0';
            else
            {
                uint8_t t = a->bus_end;
                while (t)
                {
                    pbuf[pidx++]  = '0' + (t % 10);
                    t            /= 10;
                }
            }
            while (pidx > 0)
            {
                num_str[0] = pbuf[--pidx];
                num_str[1] = '\0';
                n          = AsciiCat(line, sizeof(line), n, num_str);
            }

            LiveLogLine(log, line);
            return;
        }
    }
    LiveLogLine(log, "netlog_cfg: no MCFG in XSDT -- hiding disabled");
    hdr->netlog.enable = 0;
}
