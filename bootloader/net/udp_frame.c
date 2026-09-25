#include "udp_frame.h"

static void wr16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint16_t csum16(const uint8_t* data, size_t len, uint32_t seed)
{
    uint32_t sum = seed;
    while (len >= 2)
    {
        sum  += ((uint16_t)data[0] << 8) | data[1];
        data += 2;
        len  -= 2;
    }
    if (len)
        sum += (uint16_t)data[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

size_t udp_frame_build(uint8_t*       out,
                       size_t         out_cap,
                       const uint8_t  src_mac[6],
                       const uint8_t  dst_mac[6],
                       uint32_t       src_ip_be,
                       uint32_t       dst_ip_be,
                       uint16_t       src_port_be,
                       uint16_t       dst_port_be,
                       const uint8_t* payload,
                       size_t         payload_len)
{
    if (payload_len > UDP_FRAME_MAX_PAYLOAD)
        return 0;
    size_t frame_len = 14 + 20 + 8 + payload_len;
    if (out_cap < frame_len)
        return 0;

    for (int i = 0; i < 6; ++i)
        out[i] = dst_mac[i];
    for (int i = 0; i < 6; ++i)
        out[6 + i] = src_mac[i];
    wr16(out + 12, 0x0800);

    uint8_t* ip = out + 14;
    ip[0]       = 0x45;
    ip[1]       = 0x00;
    wr16(ip + 2, (uint16_t)(20 + 8 + payload_len));
    wr16(ip + 4, 0x0000);
    wr16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = 17;
    wr16(ip + 10, 0);

    ip[12] = (uint8_t)(src_ip_be);
    ip[13] = (uint8_t)(src_ip_be >> 8);
    ip[14] = (uint8_t)(src_ip_be >> 16);
    ip[15] = (uint8_t)(src_ip_be >> 24);
    ip[16] = (uint8_t)(dst_ip_be);
    ip[17] = (uint8_t)(dst_ip_be >> 8);
    ip[18] = (uint8_t)(dst_ip_be >> 16);
    ip[19] = (uint8_t)(dst_ip_be >> 24);

    uint16_t ipc = csum16(ip, 20, 0);
    wr16(ip + 10, ipc);

    uint8_t* udp = ip + 20;
    udp[0]       = (uint8_t)(src_port_be);
    udp[1]       = (uint8_t)(src_port_be >> 8);
    udp[2]       = (uint8_t)(dst_port_be);
    udp[3]       = (uint8_t)(dst_port_be >> 8);
    wr16(udp + 4, (uint16_t)(8 + payload_len));
    wr16(udp + 6, 0);

    for (size_t i = 0; i < payload_len; ++i)
        udp[8 + i] = payload[i];

    if (frame_len < 60)
    {
        if (out_cap < 60)
            return 0;
        for (size_t i = frame_len; i < 60; ++i)
            out[i] = 0;
        frame_len = 60;
    }

    return frame_len;
}

size_t arp_request_build(
    uint8_t* out, size_t out_cap, const uint8_t src_mac[6], uint32_t src_ip_be, uint32_t target_ip_be)
{
    size_t frame_len = 14 + 28;
    if (out_cap < frame_len)
        return 0;

    for (int i = 0; i < 6; ++i)
        out[i] = 0xFF;
    for (int i = 0; i < 6; ++i)
        out[6 + i] = src_mac[i];
    wr16(out + 12, 0x0806);

    uint8_t* a = out + 14;
    wr16(a + 0, 0x0001);
    wr16(a + 2, 0x0800);
    a[4] = 6;
    a[5] = 4;
    wr16(a + 6, 0x0001);
    for (int i = 0; i < 6; ++i)
        a[8 + i] = src_mac[i];
    a[14] = (uint8_t)src_ip_be;
    a[15] = (uint8_t)(src_ip_be >> 8);
    a[16] = (uint8_t)(src_ip_be >> 16);
    a[17] = (uint8_t)(src_ip_be >> 24);
    for (int i = 0; i < 6; ++i)
        a[18 + i] = 0;
    a[24] = (uint8_t)target_ip_be;
    a[25] = (uint8_t)(target_ip_be >> 8);
    a[26] = (uint8_t)(target_ip_be >> 16);
    a[27] = (uint8_t)(target_ip_be >> 24);

    if (frame_len < 60)
    {
        if (out_cap < 60)
            return 0;
        for (size_t i = frame_len; i < 60; ++i)
            out[i] = 0;
        frame_len = 60;
    }

    return frame_len;
}

int arp_reply_extract_mac(const uint8_t* frame, size_t len, uint32_t target_ip_be, uint8_t out_mac[6])
{
    if (len < 14 + 28)
        return -1;
    if (rd16(frame + 12) != 0x0806)
        return -1;
    const uint8_t* a = frame + 14;
    if (rd16(a + 6) != 0x0002)
        return -1;
    uint32_t sender_ip = (uint32_t)a[14] | ((uint32_t)a[15] << 8) | ((uint32_t)a[16] << 16) | ((uint32_t)a[17] << 24);
    if (sender_ip != target_ip_be)
        return -1;
    for (int i = 0; i < 6; ++i)
        out_mac[i] = a[8 + i];
    return 0;
}
