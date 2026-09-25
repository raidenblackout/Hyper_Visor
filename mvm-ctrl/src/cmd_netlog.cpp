

#include "commands.h"
#include "esp_write.h"
#include "mvm_client.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace
{

const wchar_t* usage()
{
    return L"usage: mvm-ctrl.exe netlog on|off|status";
}

int query_runtime_state()
{
    uint64_t result = 0;
    if (!mvm::do_vmmcall(VMMCALL_NETLOG_CONTROL, 2, 0, 0, &result))
        return -1;
    return (int)(result & 1);
}

int set_runtime_state(uint64_t cmd)
{
    uint64_t result = 0;
    if (!mvm::do_vmmcall(VMMCALL_NETLOG_CONTROL, cmd, 0, 0, &result))
        return -1;
    return (int)(result & 1);
}

bool persist_netlog_enable(bool on)
{
    void*     raw = nullptr;
    size_t    len = 0;
    EspResult rr  = esp_read_file(L"EFI\\mvm\\netlog.cfg", &raw, &len);
    if (rr != ESP_OK)
    {
        if (rr == ESP_FILE_MISSING)
            printf("netlog: WARNING no netlog.cfg on ESP -- can't persist. "
                   "Deploy one first (deploy-loader.ps1) then retry.\n");
        else
            printf("netlog: read netlog.cfg from ESP failed: %s\n", esp_result_str(rr));
        if (raw)
            esp_free(raw);
        return false;
    }
    if (!raw || len == 0)
    {
        printf("netlog: netlog.cfg on ESP is empty -- can't persist.\n");
        if (raw)
            esp_free(raw);
        return false;
    }

    const char* src = (const char*)raw;

    char* out = (char*)malloc(len + 32);
    if (!out)
    {
        esp_free(raw);
        return false;
    }

    size_t oi      = 0;
    bool   rewrote = false;

    for (size_t i = 0; i < len;)
    {
        size_t line_start = i;
        while (i < len && src[i] != '\n')
            ++i;
        size_t line_end = i;
        bool   has_nl   = (i < len);

        size_t k = line_start;
        while (k < line_end && (src[k] == ' ' || src[k] == '\t'))
            ++k;

        auto              tolow     = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
        static const char kKey[]    = "enable";
        const size_t      kLen      = sizeof(kKey) - 1;
        bool              is_enable = (k + kLen <= line_end);
        if (is_enable)
        {
            for (size_t j = 0; j < kLen; ++j)
            {
                if (tolow(src[k + j]) != kKey[j])
                {
                    is_enable = false;
                    break;
                }
            }
            if (is_enable)
            {
                size_t p = k + kLen;
                while (p < line_end && (src[p] == ' ' || src[p] == '\t'))
                    ++p;
                if (p >= line_end || src[p] != '=')
                    is_enable = false;
            }
        }

        if (is_enable)
        {
            const char* rep = on ? "enable=1" : "enable=0";
            size_t      rl  = strlen(rep);
            memcpy(out + oi, rep, rl);
            oi      += rl;
            rewrote  = true;
        }
        else
        {
            memcpy(out + oi, src + line_start, line_end - line_start);
            oi += (line_end - line_start);
        }
        if (has_nl)
        {
            out[oi++] = '\n';
            ++i;
        }
    }

    if (!rewrote)
    {
        if (oi > 0 && out[oi - 1] != '\n')
            out[oi++] = '\n';
        const char* rep = on ? "enable=1\n" : "enable=0\n";
        size_t      rl  = strlen(rep);
        memcpy(out + oi, rep, rl);
        oi += rl;
    }

    esp_free(raw);

    EspResult wr = esp_write_file(L"EFI\\mvm\\netlog.cfg", out, oi);
    free(out);
    if (wr != ESP_OK)
    {
        printf("netlog: write netlog.cfg to ESP failed: %s\n", esp_result_str(wr));
        return false;
    }
    return true;
}

int do_netlog(uint64_t cmd, const char* label, bool persist)
{
    const int state = set_runtime_state(cmd);
    if (state < 0)
    {
        printf("netlog: VMMCALL failed (HV not present or refused)\n");
        return 1;
    }
    printf("netlog: runtime %s -> %s\n", label, state ? "enabled" : "disabled");

    if (persist)
    {
        const bool ok = persist_netlog_enable(state != 0);
        printf("netlog: persist to netlog.cfg on ESP -> %s\n", ok ? "ok" : "FAILED");
        if (!ok)
            return 3;
    }
    return 0;
}

}

int cmd_netlog(int argc, LPWSTR* argv)
{
    const wchar_t* verb = (argc >= 3) ? argv[2] : L"status";

    if (_wcsicmp(verb, L"on") == 0)
        return do_netlog(1, "on", true);
    else if (_wcsicmp(verb, L"off") == 0)
        return do_netlog(0, "off", true);
    else if (_wcsicmp(verb, L"status") == 0)
    {
        const int state = query_runtime_state();
        if (state < 0)
        {
            printf("netlog: VMMCALL failed (HV not present)\n");
            return 1;
        }
        printf("netlog: runtime status -> %s\n", state ? "enabled" : "disabled");
        return 0;
    }

    fwprintf(stderr, L"netlog: unknown verb '%s'\n%s\n", verb, usage());
    return 2;
}
