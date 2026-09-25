

#include "commands.h"
#include "esp_write.h"
#include "mvm_client.h"

extern "C"
{
#include "hv_features_cfg.h"
}

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace
{

const wchar_t* usage()
{
    return L"usage:\n"
           L"  mvm-ctrl.exe features get [<key>]\n"
           L"  mvm-ctrl.exe features set <key> <value>\n"
           L"\n"
           L"runtime keys (flip immediate, persist on next boot):\n"
           L"  fb_log, log_mem, netlog             (0 or 1)\n"
           L"boot-time keys (require reboot):\n"
           L"  virtualize                          (0 or 1)\n"
           L"  boot_menu_timeout_ms                (uint32)\n"
           L"  intercept_shutdown                  (0 or 1)\n"
           L"  intercept_pause                     (0 or 1)\n"
           L"  intercept_svm_guard                 (0 or 1)\n"
           L"  intercept_msr_prot                  (0 or 1)  [DANGER]\n";
}

int rt_toggle(uint64_t id, uint64_t cmd)
{
    uint64_t result = 0;
    if (!mvm::do_vmmcall(VMMCALL_FEATURES_CONTROL, id, cmd, 0, &result))
        return -1;

    if (result & (uint64_t(1) << 63))
        return -1;
    return (int)(result & 1);
}

int rt_query(uint64_t id)
{
    return rt_toggle(id, 2);
}
int rt_set(uint64_t id, bool on)
{
    return rt_toggle(id, on ? 1 : 0);
}

void load_features_cfg(hv_features_v1* out)
{
    HvFeaturesDefaults(out);

    void*     raw = nullptr;
    size_t    len = 0;
    EspResult rr  = esp_read_file(L"EFI\\mvm\\hv_features.cfg", &raw, &len);
    if (rr == ESP_OK && raw)
    {
        (void)HvFeaturesParse((const char*)raw, len, out, nullptr, 0);
        esp_free(raw);
    }
    else if (rr != ESP_FILE_MISSING)
    {
        printf("features: read hv_features.cfg failed: %s\n", esp_result_str(rr));
        if (raw)
            esp_free(raw);
    }
}

bool save_features_cfg(const hv_features_v1* in)
{
    char buf[2048];
    int  n = HvFeaturesFormat(in, buf, sizeof(buf));
    if (n <= 0)
        return false;
    EspResult wr = esp_write_file(L"EFI\\mvm\\hv_features.cfg", buf, (size_t)n);
    if (wr != ESP_OK)
    {
        printf("features: write hv_features.cfg failed: %s\n", esp_result_str(wr));
        return false;
    }
    return true;
}

struct KeyDesc
{
    const wchar_t* name;
    enum
    {
        RUNTIME_BOOL,
        BOOT_BOOL,
        BOOT_U32
    } kind;
    uint64_t vmmcall_id;
    size_t   cfg_off;
    size_t   cfg_size;
};

#define OFF(m) offsetof(hv_features_v1, m)
static const KeyDesc kKeys[] = {

    { L"fb_log", KeyDesc::RUNTIME_BOOL, HV_FEATURE_ID_FB_LOG, 0, 0 },
    { L"log_mem", KeyDesc::RUNTIME_BOOL, HV_FEATURE_ID_LOG_MEM, 0, 0 },
    { L"netlog", KeyDesc::RUNTIME_BOOL, HV_FEATURE_ID_NETLOG, 0, 0 },

    { L"virtualize", KeyDesc::BOOT_BOOL, 0, OFF(virtualize), 1 },
    { L"intercept_shutdown", KeyDesc::BOOT_BOOL, 0, OFF(intercept_shutdown), 1 },
    { L"intercept_pause", KeyDesc::BOOT_BOOL, 0, OFF(intercept_pause), 1 },
    { L"intercept_svm_guard", KeyDesc::BOOT_BOOL, 0, OFF(intercept_svm_guard), 1 },
    { L"intercept_msr_prot", KeyDesc::BOOT_BOOL, 0, OFF(intercept_msr_prot), 1 },
    { L"boot_menu_timeout_ms", KeyDesc::BOOT_U32, 0, OFF(boot_menu_timeout_ms), 4 },
};
#undef OFF

const KeyDesc* find_key(const wchar_t* name)
{
    for (const auto& k : kKeys)
        if (_wcsicmp(k.name, name) == 0)
            return &k;
    return nullptr;
}

uint32_t read_boot_val(const hv_features_v1* cfg, const KeyDesc* k)
{
    const uint8_t* base = (const uint8_t*)cfg;
    if (k->cfg_size == 1)
        return *(const uint8_t*)(base + k->cfg_off);
    if (k->cfg_size == 4)
        return *(const uint32_t*)(base + k->cfg_off);
    return 0;
}

void write_boot_val(hv_features_v1* cfg, const KeyDesc* k, uint32_t v)
{
    uint8_t* base = (uint8_t*)cfg;
    if (k->cfg_size == 1)
        *(uint8_t*)(base + k->cfg_off) = (uint8_t)(v ? 1u : 0u);
    if (k->cfg_size == 4)
        *(uint32_t*)(base + k->cfg_off) = v;
}

void print_key_value(const KeyDesc* k, const hv_features_v1* cfg)
{
    char nm[64] = {};
    for (int i = 0; i < 60 && k->name[i]; ++i)
        nm[i] = (char)k->name[i];

    if (k->kind == KeyDesc::RUNTIME_BOOL)
    {
        const int rt = rt_query(k->vmmcall_id);
        if (rt < 0)
            printf("  %-22s = ? (VMMCALL failed)\n", nm);
        else
            printf("  %-22s = %d (runtime)\n", nm, rt);
    }
    else if (k->kind == KeyDesc::BOOT_BOOL)
    {
        printf("  %-22s = %u (boot-time; reboot to change)\n", nm, (unsigned)read_boot_val(cfg, k));
    }
    else
    {
        printf("  %-22s = %u (boot-time; reboot to change)\n", nm, (unsigned)read_boot_val(cfg, k));
    }
}

int cmd_get(int argc, LPWSTR* argv)
{
    hv_features_v1 cfg;
    load_features_cfg(&cfg);

    if (argc >= 4)
    {
        const KeyDesc* k = find_key(argv[3]);
        if (!k)
        {
            fwprintf(stderr, L"features: unknown key '%s'\n", argv[3]);
            return 2;
        }
        print_key_value(k, &cfg);
        return 0;
    }

    printf("features: (current values; runtime keys are what the HV reports NOW,\n"
           "          boot-time keys are what hv_features.cfg on the ESP says)\n");
    for (const auto& k : kKeys)
        print_key_value(&k, &cfg);
    return 0;
}

int cmd_set(int argc, LPWSTR* argv)
{
    if (argc < 5)
    {
        fwprintf(stderr, L"%s", usage());
        return 2;
    }
    const KeyDesc* k = find_key(argv[3]);
    if (!k)
    {
        fwprintf(stderr, L"features: unknown key '%s'\n", argv[3]);
        return 2;
    }

    uint32_t       val = 0;
    const wchar_t* v   = argv[4];
    if (_wcsicmp(v, L"on") == 0)
        val = 1;
    else if (_wcsicmp(v, L"true") == 0)
        val = 1;
    else if (_wcsicmp(v, L"off") == 0)
        val = 0;
    else if (_wcsicmp(v, L"false") == 0)
        val = 0;
    else
        val = (uint32_t)_wtoi(v);

    if (k->kind == KeyDesc::RUNTIME_BOOL && k->vmmcall_id == HV_FEATURE_ID_NETLOG)
    {
        LPWSTR forwarded[3] = { argv[0], (LPWSTR)L"netlog", (LPWSTR)(val ? L"on" : L"off") };
        return cmd_netlog(3, forwarded);
    }

    if (k->kind == KeyDesc::RUNTIME_BOOL)
    {
        const int rt = rt_set(k->vmmcall_id, val != 0);
        if (rt < 0)
        {
            printf("features: VMMCALL failed\n");
            return 1;
        }
        printf("features: runtime %ls -> %d\n", k->name, rt);

        hv_features_v1 cfg;
        load_features_cfg(&cfg);

        if (_wcsicmp(k->name, L"fb_log") == 0)
            cfg.fb_log = (uint8_t)(val ? 1 : 0);
        else if (_wcsicmp(k->name, L"log_mem") == 0)
            cfg.log_mem = (uint8_t)(val ? 1 : 0);
        const bool ok = save_features_cfg(&cfg);
        printf("features: persist hv_features.cfg -> %s\n", ok ? "ok" : "FAILED");
        return ok ? 0 : 3;
    }

    hv_features_v1 cfg;
    load_features_cfg(&cfg);
    write_boot_val(&cfg, k, val);
    const bool ok = save_features_cfg(&cfg);
    printf("features: %ls set to %u in hv_features.cfg (%s; %s)\n",
           k->name,
           (unsigned)val,
           ok ? "written" : "WRITE FAILED",
           ok ? "reboot to take effect" : "no change");
    return ok ? 0 : 3;
}

}

int cmd_features(int argc, LPWSTR* argv)
{
    if (argc < 3)
    {
        fwprintf(stderr, L"%s", usage());
        return 2;
    }
    const wchar_t* verb = argv[2];
    if (_wcsicmp(verb, L"get") == 0)
        return cmd_get(argc, argv);
    if (_wcsicmp(verb, L"set") == 0)
        return cmd_set(argc, argv);
    fwprintf(stderr, L"features: unknown verb '%s'\n%s", verb, usage());
    return 2;
}
