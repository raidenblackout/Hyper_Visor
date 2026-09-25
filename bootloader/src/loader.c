

#include "loader.h"
#include "flat_loader.h"
#include "../net/snp_send.h"

static EFI_FILE_PROTOCOL* g_snp_log_file = NULL;
static void               SnpLiveLogAdapter(const char* line)
{
    if (g_snp_log_file)
        LiveLogLine(g_snp_log_file, line);
}

int hvmem_uefi_selftest(EFI_SYSTEM_TABLE* SystemTable,
                        void*             heap_va,
                        UINT64            heap_pa,
                        UINT64            heap_size,
                        char*             status_out,
                        UINT32            status_cap);

EFI_SYSTEM_TABLE*  gST          = NULL;
EFI_BOOT_SERVICES* gBS          = NULL;
EFI_HANDLE         gImageHandle = NULL;

EFI_GUID gLoadedImageProtocolGuid    = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gSimpleFileSystemGuid       = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gDevicePathProtocolGuid     = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static const CHAR16 kDriverPath[]   = L"\\EFI\\mvm\\mvm-hv.bin";
static const CHAR16 kBootmgfwPath[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

static EFI_STATUS Chainload(EFI_DEVICE_PATH_PROTOCOL* volume_dp)
{
    EFI_DEVICE_PATH_PROTOCOL* target_dp = ComposeFilePath(volume_dp, kBootmgfwPath);
    if (!target_dp)
        return EFI_OUT_OF_RESOURCES;

    EFI_HANDLE child = NULL;
    EFI_STATUS s     = gBS->LoadImage(FALSE, gImageHandle, target_dp, NULL, 0, &child);
    gBS->FreePool(target_dp);
    if (EFI_ERROR(s))
        return s;

    return gBS->StartImage(child, NULL, NULL);
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    gImageHandle = ImageHandle;
    gST          = SystemTable;
    gBS          = SystemTable->BootServices;

    EFI_FILE_PROTOCOL* live_log = NULL;

    Print(L"mvm loader.efi\r\n");

    EFI_LOADED_IMAGE_PROTOCOL* li = NULL;
    EFI_STATUS                 s  = gBS->HandleProtocol(ImageHandle, &gLoadedImageProtocolGuid, (VOID**)&li);
    if (EFI_ERROR(s) || !li)
    {
        Print(L"  no LoadedImage; abort\r\n");
        return s ? s : EFI_LOAD_ERROR;
    }

    EFI_DEVICE_PATH_PROTOCOL* vol_dp = NULL;
    s                                = gBS->HandleProtocol(li->DeviceHandle, &gDevicePathProtocolGuid, (VOID**)&vol_dp);
    if (EFI_ERROR(s) || !vol_dp)
    {
        Print(L"  no DevicePath; abort\r\n");
        return s ? s : EFI_LOAD_ERROR;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs = NULL;
    s                                    = gBS->HandleProtocol(li->DeviceHandle, &gSimpleFileSystemGuid, (VOID**)&sfs);
    if (EFI_ERROR(s) || !sfs)
    {
        Print(L"  no SimpleFileSystem; chainload only\r\n");
        goto chainload;
    }

    EFI_FILE_PROTOCOL* root = NULL;
    s                       = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(s) || !root)
    {
        Print(L"  OpenVolume failed; chainload only\r\n");
        goto chainload;
    }

    live_log = OpenLiveLog(root);

    hv_features_v1 boot_features;
    ReadFeaturesCfg(root, live_log, &boot_features);

    BOOLEAN     skip        = FALSE;
    const char* skip_reason = NULL;

    if (!boot_features.virtualize)
    {
        skip        = TRUE;
        skip_reason = "virtualize=0 in hv_features.cfg";
    }
    else if (boot_features.boot_menu_timeout_ms > 0 && gST->ConIn && gBS->Stall)
    {
        Print(L"  Press ESC to boot Windows without HV...\r\n");

        const UINTN ticks = (boot_features.boot_menu_timeout_ms + 99u) / 100u;
        for (UINTN i = 0; i < ticks && !skip; ++i)
        {
            EFI_INPUT_KEY key = { 0 };
            EFI_STATUS    ks  = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
            if (ks == EFI_SUCCESS)
            {
                if (key.ScanCode == EFI_SCAN_ESC)
                {
                    skip        = TRUE;
                    skip_reason = "ESC pressed";
                }
            }
            gBS->Stall(100000);
        }
    }

    if (skip)
    {
        Print(L"  Skipping HV, chainloading Windows directly\r\n");
        if (live_log)
        {
            char  line[128];
            UINTN n = AsciiCat(line, sizeof(line), 0, "loader.efi: skipping HV -> Chainload (");
            n       = AsciiCat(line, sizeof(line), n, skip_reason ? skip_reason : "unknown");
            AsciiCat(line, sizeof(line), n, ")");
            LiveLogLine(live_log, line);
            LogLoadedImages(live_log);
            (void)live_log->Flush(live_log);
            (void)live_log->Close(live_log);
            live_log = NULL;
        }
        root->Close(root);
        Print(L"  chainloading bootmgfw (HV skipped)...\r\n");
        EFI_STATUS cs = Chainload(vol_dp);
        if (EFI_ERROR(cs))
        {
            Print(L"  chainload failed: ");
            PrintHex64((UINT64)cs);
            Print(L"\r\n");
        }
        return cs;
    }

    VOID* raw      = NULL;
    UINTN raw_size = 0;
    s              = ReadFile(root, kDriverPath, &raw, &raw_size);
    if (EFI_ERROR(s) || !raw)
    {
        root->Close(root);
        Print(L"  no mvm-hv.bin on ESP; chainload only\r\n");
        goto chainload;
    }

    EFI_PHYSICAL_ADDRESS hvb_pa = 0;
    s                           = ReserveHvb(&hvb_pa);
    if (EFI_ERROR(s))
    {
        root->Close(root);
        Print(L"  HVB reservation failed; chainload only\r\n");
        gBS->FreePool(raw);
        goto chainload;
    }

    void* hvb = (void*)(UINTN)hvb_pa;
    FillHvbHeader(hvb, hvb_pa);
    FillHvbFeaturesCfg(hvb, root, live_log);
    FillHvbNetlogCfg(hvb, root, live_log);
    FillHvbMcfgBase(hvb, gST, live_log);

    g_snp_log_file = live_log;
    (void)SnpSendInit(gST, &((hvb_header_t*)hvb)->netlog, SnpLiveLogAdapter);

    LogHvbBanner(live_log, hvb);

    QueryGopIntoHvb(hvb, live_log);

    {
        char   st_line[128];
        void*  heap_va   = (UINT8*)hvb + HVB_HEAP_OFFSET;
        UINT64 heap_pa64 = (UINT64)hvb_pa + HVB_HEAP_OFFSET;
        (void)hvmem_uefi_selftest(gST, heap_va, heap_pa64, HVB_HEAP_SIZE, st_line, sizeof(st_line));
    }

    {
        UINT8*             image_va   = (UINT8*)hvb + HVB_IMAGE_OFFSET;
        UINT64             image_pa64 = (UINT64)hvb_pa + HVB_IMAGE_OFFSET;
        UINT32             entry_rva  = 0;
        UINT32             image_size = 0;
        flat_load_status_t rc =
            flat_load(raw, (UINTN)raw_size, image_va, HVB_IMAGE_SIZE, image_pa64, &entry_rva, &image_size);

        gBS->FreePool(raw);
        raw = NULL;

        if (rc == FLAT_LOAD_OK)
        {
            ((hvb_header_t*)hvb)->entry_rva = entry_rva;

            typedef UINT64 (*hvrt_entry_fn)(void*, UINT64);
            hvrt_entry_fn fn = (hvrt_entry_fn)(image_va + entry_rva);

            ((hvb_header_t*)hvb)->debug_log_file_ptr = (UINT64)(UINTN)live_log;

            UINT64 rc2 = fn((void*)gST, (UINT64)hvb_pa);

            {
                char  l3[96];
                UINTN m = AsciiCat(l3, sizeof(l3), 0, "loader.efi phase6.2: hvruntime_boot_entry returned ");
                char  h3[19];
                FormatHex64(h3, rc2);
                AsciiCat(l3, sizeof(l3), m, h3);
                LiveLogLine(live_log, l3);
            }

            LogLoadedImages(live_log);
            LiveLogLine(live_log, "loader.efi phase6.2: post-HV, about to close live log and Chainload");

            ((hvb_header_t*)hvb)->debug_log_file_ptr = 0;
            if (live_log)
            {
                (void)live_log->Flush(live_log);
                (void)live_log->Close(live_log);
                live_log = NULL;
            }
        }
        else
        {
            char  line[96];
            UINTN n = AsciiCat(line, sizeof(line), 0, "loader.efi phase6.1: flat_load ");
            AsciiCat(line, sizeof(line), n, flat_load_status_str(rc));
            LiveLogLine(live_log, line);
        }
    }

    (void)WriteHvbHint(root, (UINT64)hvb_pa);

    root->Close(root);

    ((hvb_header_t*)hvb)->flags |= HVB_FLAG_LOADER_OK;

    Print(L"  HVB reserved (");
    Print(g_hvb_strategy);
    Print(L") at ");
    PrintHex64((UINT64)hvb_pa);
    Print(L"\r\n");

chainload:

    if (live_log)
    {
        LiveLogLine(live_log, "loader.efi: reaching chainload label (early bail path or normal)");
        (void)live_log->Flush(live_log);
        (void)live_log->Close(live_log);
        live_log = NULL;
    }
    Print(L"  chainloading bootmgfw...\r\n");
    s = Chainload(vol_dp);

    if (EFI_ERROR(s))
    {
        Print(L"  chainload failed: ");
        PrintHex64((UINT64)s);
        Print(L"\r\n");
    }
    return s;
}
