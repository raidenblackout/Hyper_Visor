

#include "esp_write.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

bool is_process_elevated()
{
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
        return false;
    TOKEN_ELEVATION el = {};
    DWORD           sz = 0;
    BOOL            ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz);
    CloseHandle(tok);
    return ok && el.TokenIsElevated;
}

void gen_temp_name(wchar_t out[16])
{
    unsigned r = (unsigned)GetTickCount() ^ (unsigned)GetCurrentProcessId();
    for (int i = 0; i < 12; ++i)
    {
        r                  = r * 1103515245u + 12345u;
        const unsigned nib = (r >> (i * 3)) & 0xF;
        out[i]             = (wchar_t)(nib < 10 ? L'0' + nib : L'a' + (nib - 10));
    }
    out[12] = 0;
}

bool write_dp_script(wchar_t out_path[MAX_PATH])
{
    wchar_t tmp_dir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmp_dir))
        return false;
    wchar_t suffix[16];
    gen_temp_name(suffix);
    swprintf(out_path, MAX_PATH, L"%smvm-dp-%s.txt", tmp_dir, suffix);

    HANDLE h = CreateFileW(out_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    static const char kScript[] = "select volume 2\r\n"
                                  "assign letter=S\r\n";
    DWORD             written   = 0;
    BOOL              wok       = WriteFile(h, kScript, (DWORD)(sizeof(kScript) - 1), &written, nullptr);
    CloseHandle(h);
    if (!wok)
    {
        DeleteFileW(out_path);
        return false;
    }
    return true;
}

int run_diskpart(const wchar_t* script_path)
{
    HANDLE dev_null =
        CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    wchar_t cmdline[MAX_PATH + 64];
    swprintf(cmdline, MAX_PATH + 64, L"diskpart.exe /s \"%s\"", script_path);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags      = STARTF_USESTDHANDLES;
    si.hStdInput    = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput   = dev_null != INVALID_HANDLE_VALUE ? dev_null : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError    = dev_null != INVALID_HANDLE_VALUE ? dev_null : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    int                 rc = -1;
    if (CreateProcessW(nullptr, cmdline, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 30 * 1000);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = (int)code;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (dev_null != INVALID_HANDLE_VALUE)
        CloseHandle(dev_null);
    return rc;
}

bool wait_for_s_drive()
{
    for (int i = 0; i < 40; ++i)
    {
        UINT type = GetDriveTypeW(L"S:\\");
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_REMOTE)
            return true;
        Sleep(50);
    }
    return false;
}

EspResult ensure_esp_mounted()
{
    UINT type = GetDriveTypeW(L"S:\\");
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_REMOTE)
        return ESP_OK;

    if (!is_process_elevated())
        return ESP_NOT_ADMIN;

    wchar_t script[MAX_PATH];
    if (!write_dp_script(script))
        return ESP_INTERNAL_ERROR;

    int rc = run_diskpart(script);
    DeleteFileW(script);
    if (rc < 0)
        return ESP_INTERNAL_ERROR;

    return wait_for_s_drive() ? ESP_OK : ESP_MOUNT_FAILED;
}

void build_esp_path(wchar_t out[MAX_PATH], const wchar_t* esp_rel_path)
{
    swprintf(out, MAX_PATH, L"S:\\%s", esp_rel_path);
}

bool make_parent_dirs(const wchar_t* full_path)
{
    wchar_t buf[MAX_PATH];
    wcsncpy_s(buf, MAX_PATH, full_path, _TRUNCATE);

    wchar_t* slash = wcsrchr(buf, L'\\');
    if (!slash)
        return true;
    *slash = 0;

    for (wchar_t* p = buf; *p; ++p)
    {
        if (*p == L'\\' && p != buf && (p[-1] != L':'))
        {
            *p = 0;
            CreateDirectoryW(buf, nullptr);
            *p = L'\\';
        }
    }
    CreateDirectoryW(buf, nullptr);
    return true;
}

}

const char* esp_result_str(EspResult r)
{
    switch (r)
    {
    case ESP_OK:
        return "ok";
    case ESP_NOT_ADMIN:
        return "not elevated -- run mvm-ctrl.exe from an admin console";
    case ESP_MOUNT_FAILED:
        return "ESP mount failed (diskpart couldn't assign S: to volume 2 -- run deploy-loader.ps1 once to bring the "
               "letter up, then retry)";
    case ESP_FILE_MISSING:
        return "file not present on ESP";
    case ESP_COPY_FAILED:
        return "ESP mount ok but file copy failed";
    case ESP_INTERNAL_ERROR:
        return "internal error (temp file / process spawn)";
    }
    return "unknown";
}

EspResult esp_write_file(const wchar_t* esp_rel_path, const void* data, size_t len)
{
    EspResult mr = ensure_esp_mounted();
    if (mr != ESP_OK)
        return mr;

    wchar_t dst[MAX_PATH];
    build_esp_path(dst, esp_rel_path);
    make_parent_dirs(dst);

    HANDLE h = CreateFileW(dst, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ESP_COPY_FAILED;

    DWORD written = 0;
    BOOL  wok     = WriteFile(h, data, (DWORD)len, &written, nullptr);
    CloseHandle(h);
    if (!wok || written != len)
        return ESP_COPY_FAILED;
    return ESP_OK;
}

EspResult esp_read_file(const wchar_t* esp_rel_path, void** out_data, size_t* out_len)
{
    if (out_data)
        *out_data = nullptr;
    if (out_len)
        *out_len = 0;

    EspResult mr = ensure_esp_mounted();
    if (mr != ESP_OK)
        return mr;

    wchar_t src[MAX_PATH];
    build_esp_path(src, esp_rel_path);

    HANDLE h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return ESP_FILE_MISSING;
        return ESP_INTERNAL_ERROR;
    }

    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (LONGLONG)(16 * 1024 * 1024))
    {
        CloseHandle(h);
        return ESP_INTERNAL_ERROR;
    }

    void* buf = malloc((size_t)sz.QuadPart + 1);
    if (!buf)
    {
        CloseHandle(h);
        return ESP_INTERNAL_ERROR;
    }

    DWORD read = 0;
    ReadFile(h, buf, (DWORD)sz.QuadPart, &read, nullptr);
    ((char*)buf)[sz.QuadPart] = 0;
    CloseHandle(h);

    if (out_data)
        *out_data = buf;
    if (out_len)
        *out_len = (size_t)sz.QuadPart;
    return ESP_OK;
}

void esp_free(void* p)
{
    free(p);
}
