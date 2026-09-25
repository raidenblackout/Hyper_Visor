

#ifndef LOADER_H
#define LOADER_H

#include "efi/efi.h"
#include "hvb.h"

typedef unsigned __int64 size_t;

void* memset(void* dest, int value, size_t count);
void* memcpy(void* dest, const void* src, size_t count);

extern EFI_SYSTEM_TABLE*  gST;
extern EFI_BOOT_SERVICES* gBS;
extern EFI_HANDLE         gImageHandle;

extern EFI_GUID gLoadedImageProtocolGuid;
extern EFI_GUID gSimpleFileSystemGuid;
extern EFI_GUID gDevicePathProtocolGuid;
extern EFI_GUID gGraphicsOutputProtocolGuid;

extern const CHAR16* g_hvb_strategy;

void  Print(const CHAR16* s);
void  PrintHex64(UINT64 v);
void  FormatHex64(char* out, UINT64 v);
UINTN AsciiCat(char* dst, UINTN dst_cap, UINTN dst_len, const char* s);
UINTN AsciiCatHexBytes(char* dst, UINTN cap, UINTN len, const UINT8* src, UINTN count);
UINTN Char16Len(const CHAR16* s);

UINT16  DpNodeLen(EFI_DEVICE_PATH_PROTOCOL* n);
BOOLEAN DpIsEnd(EFI_DEVICE_PATH_PROTOCOL* n);
UINTN   DpBytesUntilEnd(EFI_DEVICE_PATH_PROTOCOL* dp);

EFI_DEVICE_PATH_PROTOCOL* ComposeFilePath(EFI_DEVICE_PATH_PROTOCOL* volume_dp, const CHAR16* file);

EFI_STATUS ReadFile(EFI_FILE_PROTOCOL* root, const CHAR16* path, VOID** out_buf, UINTN* out_size);

EFI_STATUS ReserveHvb(EFI_PHYSICAL_ADDRESS* out_pa);
EFI_STATUS WriteHvbHint(EFI_FILE_PROTOCOL* root, UINT64 hvb_pa);
void       FillHvbHeader(void* hvb_base, EFI_PHYSICAL_ADDRESS hvb_pa);
void       QueryGopIntoHvb(void* hvb, EFI_FILE_PROTOCOL* diag);
void       FillHvbNetlogCfg(void* hvb, EFI_FILE_PROTOCOL* root, EFI_FILE_PROTOCOL* log);
void       FillHvbFeaturesCfg(void* hvb, EFI_FILE_PROTOCOL* root, EFI_FILE_PROTOCOL* log);
void       ReadFeaturesCfg(EFI_FILE_PROTOCOL* root, EFI_FILE_PROTOCOL* log, hv_features_v1* out);
void       FillHvbMcfgBase(void* hvb, EFI_SYSTEM_TABLE* st, EFI_FILE_PROTOCOL* log);
UINT64     ParseHexPa(const char* s, UINTN n);

EFI_FILE_PROTOCOL* OpenLiveLog(EFI_FILE_PROTOCOL* root);
void               LiveLogLine(EFI_FILE_PROTOCOL* file, const char* line);
void               LogHvbBanner(EFI_FILE_PROTOCOL* file, void* hvb);
void LogLoadedImages(EFI_FILE_PROTOCOL* file);

#endif
