
#ifndef NETLOG_CFG_H
#define NETLOG_CFG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        NETLOG_MODE_DISABLED  = 0,
        NETLOG_MODE_BOOT_ONLY = 0,
        NETLOG_MODE_POST_ONLY = 1,
        NETLOG_MODE_BOTH      = 2
    };

#pragma pack(push, 1)
    typedef struct netlog_cfg_v4
    {
        uint32_t enable;
        uint32_t mode;
        uint32_t dest_ip_be;
        uint32_t src_ip_be;
        uint32_t src_netmask_be;
        uint32_t src_gateway_be;
        uint16_t dest_port_be;
        uint16_t syslog_pri;
        uint16_t snp_bdf;
        uint16_t hv_bdf;
        uint8_t  gateway_mac[6];
        uint8_t  gateway_mac_locked;
        uint8_t  _pad0;
        uint64_t mcfg_base_pa;
    } netlog_cfg_v4;
#pragma pack(pop)

#ifdef __cplusplus
    static_assert(sizeof(netlog_cfg_v4) == 48, "netlog_cfg_v4 size drift");
#else
typedef char _netlog_cfg_v4_size_assert[sizeof(netlog_cfg_v4) == 48 ? 1 : -1];
#endif

    int NetlogCfgParse(const char* text, size_t len, netlog_cfg_v4* out, char* err, size_t err_cap);

    int NetlogCfgFormatBdf(uint16_t bdf, char out[9]);

#ifdef __cplusplus
}
#endif

#endif
