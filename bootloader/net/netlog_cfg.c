
#include "netlog_cfg.h"

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}
static int is_dig(char c)
{
    return c >= '0' && c <= '9';
}
static int is_hex(char c)
{
    return is_dig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static const char* skip_ws(const char* p, const char* end)
{
    while (p < end && is_ws(*p))
        ++p;
    return p;
}

static int memcmp_local(const void* s1, const void* s2, size_t n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

static int parse_ipv4(const char* s, const char* e, uint32_t* out_be)
{
    uint32_t v      = 0;
    int      octets = 0;
    while (s < e && octets < 4)
    {
        if (!is_dig(*s))
            return -1;
        uint32_t n      = 0;
        int      digits = 0;
        while (s < e && is_dig(*s) && digits < 3)
        {
            n = n * 10 + (*s - '0');
            ++s;
            ++digits;
        }
        if (n > 255)
            return -1;
        v = (v << 8) | n;
        ++octets;
        if (octets == 4)
            break;
        if (s >= e || *s != '.')
            return -1;
        ++s;
    }
    if (octets != 4 || s != e)
        return -1;

    *out_be = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
    return 0;
}

static int parse_bdf(const char* s, const char* e, uint16_t* out)
{
    if (e - s < 6 || e - s > 8)
        return -1;
    uint32_t bus = 0, dev = 0, fn = 0;
    int      i = 0;
    while (s < e && is_hex(*s) && i < 2)
    {
        bus = bus * 16 + (is_dig(*s) ? *s - '0' : (lower(*s) - 'a' + 10));
        ++s;
        ++i;
    }
    if (s >= e || *s != ':')
        return -1;
    ++s;
    i = 0;
    while (s < e && is_hex(*s) && i < 2)
    {
        dev = dev * 16 + (is_dig(*s) ? *s - '0' : (lower(*s) - 'a' + 10));
        ++s;
        ++i;
    }
    if (s >= e || *s != '.')
        return -1;
    ++s;
    if (s >= e || !is_dig(*s))
        return -1;
    fn = (uint32_t)(*s - '0');
    ++s;
    if (s != e)
        return -1;
    if (bus > 255 || dev > 31 || fn > 7)
        return -1;
    *out = (uint16_t)((bus << 8) | (dev << 3) | fn);
    return 0;
}

static int parse_mac(const char* s, const char* e, uint8_t out[6])
{
    if (e - s == 4 && lower(s[0]) == 'a' && lower(s[1]) == 'u' && lower(s[2]) == 't' && lower(s[3]) == 'o')
    {
        for (int i = 0; i < 6; ++i)
            out[i] = 0;
        return 0;
    }
    if (e - s != 17)
        return -1;
    for (int i = 0; i < 6; ++i)
    {
        if (!is_hex(s[0]) || !is_hex(s[1]))
            return -1;
        uint32_t hi  = is_dig(s[0]) ? s[0] - '0' : lower(s[0]) - 'a' + 10;
        uint32_t lo  = is_dig(s[1]) ? s[1] - '0' : lower(s[1]) - 'a' + 10;
        out[i]       = (uint8_t)((hi << 4) | lo);
        s           += 2;
        if (i < 5)
        {
            if (*s != ':')
                return -1;
            ++s;
        }
    }
    return 0;
}

static int parse_u32(const char* s, const char* e, uint32_t* out)
{
    if (s >= e)
        return -1;
    uint64_t v = 0;
    while (s < e)
    {
        if (!is_dig(*s))
            return -1;
        v = v * 10 + (*s - '0');
        if (v > 0xFFFFFFFFu)
            return -1;
        ++s;
    }
    *out = (uint32_t)v;
    return 0;
}

int NetlogCfgFormatBdf(uint16_t bdf, char out[9])
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t           bus = (uint8_t)(bdf >> 8), dev = (uint8_t)((bdf >> 3) & 0x1F), fn = (uint8_t)(bdf & 0x7);
    out[0] = hex[bus >> 4];
    out[1] = hex[bus & 0xF];
    out[2] = ':';
    out[3] = hex[dev >> 4];
    out[4] = hex[dev & 0xF];
    out[5] = '.';
    out[6] = hex[fn];
    out[7] = 0;
    out[8] = 0;
    return 0;
}

static int copy_err(char* err, size_t cap, const char* msg, int lineno)
{
    if (!err || cap < 32)
        return -1;

    const char* p = "line ";
    size_t      i = 0;
    while (*p && i + 1 < cap)
        err[i++] = *p++;

    char tmp[8];
    int  t = 0;
    if (lineno == 0)
        tmp[t++] = '0';
    else
    {
        int n = lineno;
        while (n)
        {
            tmp[t++]  = (char)('0' + n % 10);
            n        /= 10;
        }
    }
    while (t > 0 && i + 1 < cap)
        err[i++] = tmp[--t];
    p = ": ";
    while (*p && i + 1 < cap)
        err[i++] = *p++;
    while (*msg && i + 1 < cap)
        err[i++] = *msg++;
    err[i] = 0;
    return 0;
}

int NetlogCfgParse(const char* text, size_t len, netlog_cfg_v4* out, char* err, size_t err_cap)
{
    for (size_t i = 0; i < sizeof(*out); ++i)
        ((uint8_t*)out)[i] = 0;

    const char *p = text, *end = text + len;
    int         lineno = 0;
    while (p < end)
    {
        ++lineno;
        const char* ln = p;
        while (p < end && *p != '\n')
            ++p;
        const char* le = p;
        if (p < end)
            ++p;

        while (le > ln && (le[-1] == '\r' || le[-1] == ' ' || le[-1] == '\t'))
            --le;

        for (const char* c = ln; c < le; ++c)
            if (*c == ';' || *c == '#')
            {
                le = c;
                while (le > ln && (le[-1] == ' ' || le[-1] == '\t'))
                    --le;
                break;
            }

        const char* ks = skip_ws(ln, le);
        if (ks == le)
            continue;

        const char* ke = ks;
        while (ke < le && *ke != '=' && !is_ws(*ke))
            ++ke;
        const char* eq = skip_ws(ke, le);
        if (eq == le || *eq != '=')
        {
            copy_err(err, err_cap, "missing '='", lineno);
            goto fail;
        }
        const char* vs = skip_ws(eq + 1, le);
        const char* ve = le;

        size_t klen = (size_t)(ke - ks);
        int    rc   = 0;
        if (klen == 6 && ks[0] == 'e' && ks[1] == 'n' && ks[2] == 'a' && ks[3] == 'b' && ks[4] == 'l' && ks[5] == 'e')
            rc = parse_u32(vs, ve, &out->enable);
        else if (klen == 4 && ks[0] == 'm' && ks[1] == 'o' && ks[2] == 'd' && ks[3] == 'e')
        {
            size_t vlen = (size_t)(ve - vs);
            if (vlen == 9 && !memcmp_local(vs, "boot_only", 9))
                out->mode = NETLOG_MODE_BOOT_ONLY;
            else if (vlen == 9 && !memcmp_local(vs, "post_only", 9))
                out->mode = NETLOG_MODE_POST_ONLY;
            else if (vlen == 4 && !memcmp_local(vs, "both", 4))
                out->mode = NETLOG_MODE_BOTH;
            else
            {
                rc = -1;
            }
        }
        else if (klen == 7 && !memcmp_local(ks, "dest_ip", 7))
            rc = parse_ipv4(vs, ve, &out->dest_ip_be);
        else if (klen == 6 && !memcmp_local(ks, "src_ip", 6))
            rc = parse_ipv4(vs, ve, &out->src_ip_be);
        else if (klen == 11 && !memcmp_local(ks, "src_netmask", 11))
            rc = parse_ipv4(vs, ve, &out->src_netmask_be);
        else if (klen == 11 && !memcmp_local(ks, "src_gateway", 11))
            rc = parse_ipv4(vs, ve, &out->src_gateway_be);
        else if (klen == 9 && !memcmp_local(ks, "dest_port", 9))
        {
            uint32_t p32;
            rc = parse_u32(vs, ve, &p32);
            if (!rc && p32 > 0xFFFF)
                rc = -1;
            if (!rc)
                out->dest_port_be = (uint16_t)((p32 << 8) | (p32 >> 8));
        }
        else if (klen == 10 && !memcmp_local(ks, "syslog_pri", 10))
        {
            uint32_t v;
            rc = parse_u32(vs, ve, &v);
            if (!rc)
                out->syslog_pri = (uint16_t)v;
        }
        else if (klen == 7 && !memcmp_local(ks, "snp_bdf", 7))
            rc = parse_bdf(vs, ve, &out->snp_bdf);
        else if (klen == 6 && !memcmp_local(ks, "hv_bdf", 6))
            rc = parse_bdf(vs, ve, &out->hv_bdf);
        else if (klen == 11 && !memcmp_local(ks, "gateway_mac", 11))
        {
            rc                      = parse_mac(vs, ve, out->gateway_mac);
            out->gateway_mac_locked = (out->gateway_mac[0] | out->gateway_mac[1] | out->gateway_mac[2] |
                                       out->gateway_mac[3] | out->gateway_mac[4] | out->gateway_mac[5])
                                          ? 1
                                          : 0;
        }
        else
        {
            copy_err(err, err_cap, "unknown key", lineno);
            goto fail;
        }

        if (rc)
        {
            copy_err(err, err_cap, "value parse error", lineno);
            goto fail;
        }
    }
    return 0;

fail:
    for (size_t i = 0; i < sizeof(*out); ++i)
        ((uint8_t*)out)[i] = 0;
    return -1;
}
