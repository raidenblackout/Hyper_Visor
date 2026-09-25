#ifndef UDP_FRAME_H
#define UDP_FRAME_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UDP_FRAME_MAX_PAYLOAD 1450

    size_t udp_frame_build(uint8_t*       out,
                           size_t         out_cap,
                           const uint8_t  src_mac[6],
                           const uint8_t  dst_mac[6],
                           uint32_t       src_ip_be,
                           uint32_t       dst_ip_be,
                           uint16_t       src_port_be,
                           uint16_t       dst_port_be,
                           const uint8_t* payload,
                           size_t         payload_len);

    size_t arp_request_build(
        uint8_t* out, size_t out_cap, const uint8_t src_mac[6], uint32_t src_ip_be, uint32_t target_ip_be);

    int arp_reply_extract_mac(const uint8_t* frame, size_t len, uint32_t target_ip_be, uint8_t out_mac[6]);

#ifdef __cplusplus
}
#endif
#endif
