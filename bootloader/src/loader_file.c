

#include "loader.h"

EFI_DEVICE_PATH_PROTOCOL* ComposeFilePath(EFI_DEVICE_PATH_PROTOCOL* volume_dp, const CHAR16* file)
{
    UINTN vol_len     = DpBytesUntilEnd(volume_dp);
    UINTN str_len     = Char16Len(file);
    UINTN fp_node_len = 4 + (str_len + 1) * 2;
    UINTN total       = vol_len + fp_node_len + 4;

    VOID* buf = NULL;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, total, &buf)) || !buf)
        return NULL;

    UINT8* out = (UINT8*)buf;
    memcpy(out, volume_dp, vol_len);

    UINT8* node   = out + vol_len;
    node[0]       = 4;
    node[1]       = 4;
    node[2]       = (UINT8)(fp_node_len & 0xFF);
    node[3]       = (UINT8)((fp_node_len >> 8) & 0xFF);
    CHAR16* fpstr = (CHAR16*)(node + 4);
    for (UINTN i = 0; i <= str_len; ++i)
        fpstr[i] = file[i];

    UINT8* end = node + fp_node_len;
    end[0]     = 0x7F;
    end[1]     = 0xFF;
    end[2]     = 4;
    end[3]     = 0;

    return (EFI_DEVICE_PATH_PROTOCOL*)buf;
}

EFI_STATUS ReadFile(EFI_FILE_PROTOCOL* root, const CHAR16* path, VOID** out_buf, UINTN* out_size)
{
    *out_buf  = NULL;
    *out_size = 0;

    EFI_FILE_PROTOCOL* file = NULL;
    EFI_STATUS         s    = root->Open(root, &file, (CHAR16*)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s))
        return s;

    UINT64 size = 0;
    s           = file->SetPosition(file, 0xFFFFFFFFFFFFFFFFULL);
    if (EFI_ERROR(s))
    {
        file->Close(file);
        return s;
    }
    s = file->GetPosition(file, &size);
    if (EFI_ERROR(s))
    {
        file->Close(file);
        return s;
    }
    s = file->SetPosition(file, 0);
    if (EFI_ERROR(s))
    {
        file->Close(file);
        return s;
    }

    if (size == 0 || size > HVB_IMAGE_SIZE)
    {
        file->Close(file);
        return EFI_BAD_BUFFER_SIZE;
    }

    VOID* buf = NULL;
    s         = gBS->AllocatePool(EfiLoaderData, (UINTN)size, &buf);
    if (EFI_ERROR(s) || !buf)
    {
        file->Close(file);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN got = (UINTN)size;
    s         = file->Read(file, &got, buf);
    file->Close(file);
    if (EFI_ERROR(s) || got != size)
    {
        gBS->FreePool(buf);
        return EFI_DEVICE_ERROR;
    }

    *out_buf  = buf;
    *out_size = (UINTN)size;
    return EFI_SUCCESS;
}
