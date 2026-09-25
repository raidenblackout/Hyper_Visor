#include "snp_send.h"
#include "udp_frame.h"
#include "../include/efi/efi.h"

static EFI_BOOT_SERVICES*           g_bs  = NULL;
static EFI_SIMPLE_NETWORK_PROTOCOL* g_snp = NULL;
static netlog_cfg_v4*               g_cfg = NULL;
static void (*g_log)(const char*)         = NULL;
static uint8_t  g_src_mac[6];
static uint8_t  g_dst_mac[6];
static int      g_armed       = 0;
static uint32_t g_drops       = 0;
static uint16_t g_src_port_be = 0;

int SnpSendIsArmed(void)
{
    return g_armed;
}

static void snp_log(const char* msg)
{
    if (g_log)
        g_log(msg);
}

static int locate_snp_matching_bdf(EFI_SYSTEM_TABLE* st, uint16_t bdf, EFI_SIMPLE_NETWORK_PROTOCOL** out_snp)
{
    (void)st;
    EFI_GUID    snpGuid      = EFI_SIMPLE_NETWORK_PROTOCOL_GUID_LITERAL;
    EFI_GUID    pciGuid      = EFI_PCI_IO_PROTOCOL_GUID_LITERAL;
    EFI_HANDLE* handles      = NULL;
    UINTN       handle_count = 0;
    EFI_STATUS  s            = g_bs->LocateHandleBuffer(ByProtocol, &snpGuid, NULL, &handle_count, &handles);
    if (EFI_ERROR(s) || handle_count == 0)
        return -1;

    int found = -1;
    for (UINTN i = 0; i < handle_count; ++i)
    {
        EFI_SIMPLE_NETWORK_PROTOCOL* snp = NULL;
        if (EFI_ERROR(g_bs->HandleProtocol(handles[i], &snpGuid, (void**)&snp)) || !snp)
            continue;

        if (bdf != 0)
        {
            EFI_PCI_IO_PROTOCOL* pci = NULL;
            if (EFI_ERROR(g_bs->HandleProtocol(handles[i], &pciGuid, (void**)&pci)) || !pci)
                continue;
            UINTN seg = 0, bus = 0, dev = 0, fn = 0;
            if (EFI_ERROR(pci->GetLocation(pci, &seg, &bus, &dev, &fn)))
                continue;
            uint16_t got = (uint16_t)((bus << 8) | (dev << 3) | fn);
            if (got != bdf)
                continue;
        }
        *out_snp = snp;
        found    = 0;
        break;
    }
    g_bs->FreePool(handles);
    return found;
}

static int arp_resolve_gateway(void)
{
    uint8_t req[64];
    size_t  req_len = arp_request_build(req, sizeof(req), g_src_mac, g_cfg->src_ip_be, g_cfg->src_gateway_be);
    if (!req_len)
        return -1;

    EFI_STATUS s = g_snp->Transmit(g_snp, 0, req_len, req, NULL, NULL, NULL);
    if (EFI_ERROR(s))
    {
        snp_log("snp: ARP Transmit failed");
        return -1;
    }

    for (int poll = 0; poll < 50; ++poll)
    {
        uint8_t rx[512];
        UINTN   rx_len = sizeof(rx);
        s              = g_snp->Receive(g_snp, NULL, &rx_len, rx, NULL, NULL, NULL);
        if (s == 0 && rx_len >= 42)
        {
            if (arp_reply_extract_mac(rx, rx_len, g_cfg->src_gateway_be, g_dst_mac) == 0)
            {
                for (int i = 0; i < 6; ++i)
                    g_cfg->gateway_mac[i] = g_dst_mac[i];
                g_cfg->gateway_mac_locked = 1;
                return 0;
            }
        }
        g_bs->Stall(10000);
    }
    snp_log("snp: ARP timeout");
    return -1;
}

int SnpSendInit(void* system_table, netlog_cfg_v4* cfg, void (*log_fn)(const char*))
{
    if (g_armed)
        return 0;

    EFI_SYSTEM_TABLE* st = (EFI_SYSTEM_TABLE*)system_table;
    g_bs                 = st->BootServices;
    g_cfg                = cfg;
    g_log                = log_fn;
    g_armed              = 0;
    g_drops              = 0;

    if (!cfg->enable)
    {
        snp_log("snp: netlog disabled -- skip init");
        return -1;
    }
    if (cfg->mode == NETLOG_MODE_POST_ONLY)
    {
        snp_log("snp: mode=post_only -- boot-time send disabled");
        return -1;
    }

    if (locate_snp_matching_bdf(st, cfg->snp_bdf, &g_snp) != 0)
    {
        snp_log("snp: no matching SNP handle for snp_bdf");
        return -1;
    }

    if (g_snp->Mode->State == EfiSimpleNetworkStopped)
    {
        if (EFI_ERROR(g_snp->Start(g_snp)))
        {
            snp_log("snp: Start failed");
            return -1;
        }
    }
    if (g_snp->Mode->State == EfiSimpleNetworkStarted)
    {
        if (EFI_ERROR(g_snp->Initialize(g_snp, 0, 0)))
        {
            snp_log("snp: Initialize failed");
            return -1;
        }
    }

    for (int i = 0; i < 6; ++i)
        g_src_mac[i] = g_snp->Mode->CurrentAddress.Addr[i];
    g_src_port_be = 0x8515;

    if (cfg->gateway_mac_locked)
        for (int i = 0; i < 6; ++i)
            g_dst_mac[i] = cfg->gateway_mac[i];
    else if (arp_resolve_gateway() != 0)
        return -1;

    char              line[128] = "snp: armed src_mac=00:00:00:00:00:00 gw_mac=00:00:00:00:00:00";
    static const char hex[]     = "0123456789abcdef";
    for (int i = 0; i < 6; ++i)
    {
        line[19 + i * 3]     = hex[g_src_mac[i] >> 4];
        line[19 + i * 3 + 1] = hex[g_src_mac[i] & 0xF];
        line[43 + i * 3]     = hex[g_dst_mac[i] >> 4];
        line[43 + i * 3 + 1] = hex[g_dst_mac[i] & 0xF];
    }
    snp_log(line);

    g_armed = 1;
    return 0;
}

void SnpSendUdpLine(const char* line, size_t len)
{
    if (!g_armed)
        return;
    if (len > UDP_FRAME_MAX_PAYLOAD)
        len = UDP_FRAME_MAX_PAYLOAD;

    uint8_t frame[UDP_FRAME_MAX_PAYLOAD + 64];
    size_t  flen = udp_frame_build(frame,
                                   sizeof(frame),
                                   g_src_mac,
                                   g_dst_mac,
                                   g_cfg->src_ip_be,
                                   g_cfg->dest_ip_be,
                                   g_src_port_be,
                                   g_cfg->dest_port_be,
                                   (const uint8_t*)line,
                                   len);
    if (!flen)
    {
        ++g_drops;
        return;
    }

    EFI_STATUS s = g_snp->Transmit(g_snp, 0, flen, frame, NULL, NULL, NULL);
    if (EFI_ERROR(s))
    {
        ++g_drops;
        return;
    }

    for (int poll = 0; poll < 100; ++poll)
    {
        UINT32 istatus = 0;
        void*  txbuf   = NULL;
        s              = g_snp->GetStatus(g_snp, &istatus, &txbuf);
        if (s == 0 && txbuf == frame)
            return;
        if (s != 0)
            return;
    }
    ++g_drops;
}

void SnpSendShutdown(void)
{
    if (g_snp)
    {
        (void)g_snp->Shutdown(g_snp);
        (void)g_snp->Stop(g_snp);
    }
    g_armed = 0;
    g_snp   = NULL;
    g_cfg   = NULL;
}
