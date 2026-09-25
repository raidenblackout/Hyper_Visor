

#include "hv_features_cfg.h"

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}
static int is_dig(char c)
{
    return c >= '0' && c <= '9';
}
static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int str_eq_ci(const char* a, size_t alen, const char* lit)
{
    size_t i = 0;
    for (; i < alen; ++i)
    {
        if (!lit[i])
            return 0;
        if (to_lower(a[i]) != lit[i])
            return 0;
    }
    return lit[i] == 0;
}

static const char* skip_ws(const char* p, const char* end)
{
    while (p < end && is_ws(*p))
        ++p;
    return p;
}

static const char* parse_u32(const char* s, const char* e, uint32_t* out)
{
    if (s >= e || !is_dig(*s))
        return 0;
    uint32_t v = 0;
    while (s < e && is_dig(*s))
    {
        uint32_t n = v * 10u + (uint32_t)(*s - '0');
        if (n < v)
            return 0;
        v = n;
        ++s;
    }
    *out = v;
    return s;
}

void HvFeaturesDefaults(hv_features_v1* out)
{
    if (!out)
        return;
    for (unsigned i = 0; i < sizeof(*out); ++i)
        ((unsigned char*)out)[i] = 0;
    out->version              = HV_FEATURES_VERSION;
    out->fb_log               = 1;
    out->log_mem              = 1;
    out->virtualize           = 1;
    out->intercept_shutdown   = 1;
    out->intercept_pause      = 1;
    out->intercept_svm_guard  = 1;
    out->intercept_msr_prot   = 0;
    out->boot_menu_timeout_ms = 5000;
}

static void set_bool_field(hv_features_v1* out, const char* k, size_t klen, uint32_t v)
{
    uint8_t b = (v != 0) ? 1u : 0u;

    if (str_eq_ci(k, klen, "fb_log"))
        out->fb_log = b;
    else if (str_eq_ci(k, klen, "log_mem"))
        out->log_mem = b;
    else if (str_eq_ci(k, klen, "virtualize"))
        out->virtualize = b;
    else if (str_eq_ci(k, klen, "intercept_shutdown"))
        out->intercept_shutdown = b;
    else if (str_eq_ci(k, klen, "intercept_pause"))
        out->intercept_pause = b;
    else if (str_eq_ci(k, klen, "intercept_svm_guard"))
        out->intercept_svm_guard = b;
    else if (str_eq_ci(k, klen, "intercept_msr_prot"))
        out->intercept_msr_prot = b;
    else if (str_eq_ci(k, klen, "boot_menu_timeout_ms"))
        out->boot_menu_timeout_ms = v;
}

int HvFeaturesParse(const char* text, size_t len, hv_features_v1* out, char* err, size_t err_cap)
{
    if (!text || !out)
        return -1;
    (void)err;
    (void)err_cap;

    const char* p   = text;
    const char* end = text + len;

    while (p < end)
    {
        p = skip_ws(p, end);
        if (p >= end)
            break;

        if (*p == '#' || *p == '\n')
        {
            while (p < end && *p != '\n')
                ++p;
            if (p < end)
                ++p;
            continue;
        }

        const char* k = p;
        while (p < end && *p != '=' && *p != '\n' && !is_ws(*p))
            ++p;
        size_t klen = (size_t)(p - k);
        if (klen == 0)
        {
            while (p < end && *p != '\n')
                ++p;
            if (p < end)
                ++p;
            continue;
        }

        p = skip_ws(p, end);
        if (p >= end || *p != '=')
        {
            while (p < end && *p != '\n')
                ++p;
            if (p < end)
                ++p;
            continue;
        }
        ++p;
        p = skip_ws(p, end);

        uint32_t    v  = 0;
        const char* ve = parse_u32(p, end, &v);
        if (!ve)
        {
            while (p < end && *p != '\n')
                ++p;
            if (p < end)
                ++p;
            continue;
        }
        p = ve;

        set_bool_field(out, k, klen, v);

        while (p < end && *p != '\n')
            ++p;
        if (p < end)
            ++p;
    }
    return 0;
}

static size_t append_kv(char* buf, size_t cap, size_t off, const char* key, uint32_t val)
{
    for (const char* s = key; *s; ++s)
    {
        if (off >= cap)
            return (size_t)-1;
        buf[off++] = *s;
    }
    if (off >= cap)
        return (size_t)-1;
    buf[off++] = '=';

    char tmp[11];
    int  ti = 0;
    if (val == 0)
        tmp[ti++] = '0';
    else
        while (val > 0)
        {
            tmp[ti++]  = (char)('0' + (val % 10u));
            val       /= 10u;
        }
    while (ti > 0)
    {
        if (off >= cap)
            return (size_t)-1;
        buf[off++] = tmp[--ti];
    }

    if (off >= cap)
        return (size_t)-1;
    buf[off++] = '\n';
    return off;
}

static size_t append_str(char* buf, size_t cap, size_t off, const char* s)
{
    for (; *s; ++s)
    {
        if (off >= cap)
            return (size_t)-1;
        buf[off++] = *s;
    }
    return off;
}

int HvFeaturesFormat(const hv_features_v1* in, char* buf, size_t cap)
{
    if (!in || !buf || cap == 0)
        return -1;
    size_t o = 0;

#define APPEND_STR(s)                     \
    do                                    \
    {                                     \
        o = append_str(buf, cap, o, (s)); \
        if (o == (size_t)-1)              \
            return -1;                    \
    } while (0)
#define APPEND_KV(k, v)                                 \
    do                                                  \
    {                                                   \
        o = append_kv(buf, cap, o, (k), (uint32_t)(v)); \
        if (o == (size_t)-1)                            \
            return -1;                                  \
    } while (0)

    APPEND_STR("# mvm HV feature toggles. Regenerated by mvm-ctrl.\n");
    APPEND_STR("# One key=value per line. '#' starts a comment. Booleans: 0 or 1.\n");
    APPEND_STR("# Missing keys fall back to compile-time defaults.\n");
    APPEND_STR("\n");
    APPEND_STR("# ---- Runtime toggles (flippable via `mvm-ctrl features set`) ----\n");
    APPEND_STR("# (netlog toggle lives in netlog.cfg, not here -- see hv_features_cfg.h.)\n");
    APPEND_KV("fb_log", in->fb_log);
    APPEND_KV("log_mem", in->log_mem);
    APPEND_STR("\n");
    APPEND_STR("# ---- Boot-time toggles (require reboot to take effect) ----\n");
    APPEND_KV("virtualize", in->virtualize);
    APPEND_KV("boot_menu_timeout_ms", in->boot_menu_timeout_ms);
    APPEND_KV("intercept_shutdown", in->intercept_shutdown);
    APPEND_KV("intercept_pause", in->intercept_pause);
    APPEND_KV("intercept_svm_guard", in->intercept_svm_guard);
    APPEND_STR("# WARNING: intercept_msr_prot=1 reproduces the CPU-11 EAC-probe wedge.\n");
    APPEND_KV("intercept_msr_prot", in->intercept_msr_prot);

#undef APPEND_KV
#undef APPEND_STR
    return (int)o;
}
