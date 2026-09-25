
#pragma once
#include <cstdint>
#include "net/pci.h"

namespace rtl8125
{

constexpr uint16_t VENDOR_REALTEK    = 0x10EC;
constexpr uint16_t DEVICE_RTL8125    = 0x8125;
constexpr uint16_t DEVICE_RTL8125_BG = 0x3000;

namespace reg
{
constexpr uint32_t IDR0       = 0x00;
constexpr uint32_t IDR4       = 0x04;
constexpr uint32_t TNPDS_LOW  = 0x20;
constexpr uint32_t TNPDS_HIGH = 0x24;
constexpr uint32_t INT_CFG0   = 0x34;
constexpr uint32_t CR         = 0x37;
constexpr uint32_t IMR0       = 0x38;
constexpr uint32_t ISR0       = 0x3C;
constexpr uint32_t TX_CONFIG  = 0x40;
constexpr uint32_t RX_CONFIG  = 0x44;
constexpr uint32_t CFG9346    = 0x50;
constexpr uint32_t CONFIG1    = 0x52;
constexpr uint32_t CONFIG2    = 0x53;
constexpr uint32_t CONFIG3    = 0x54;
constexpr uint32_t CONFIG5    = 0x56;
constexpr uint32_t PHY_STATUS = 0x6C;
constexpr uint32_t PMCH       = 0x6F;
constexpr uint32_t INT_CFG1   = 0x7A;
constexpr uint32_t TPPOLL     = 0x90;
constexpr uint32_t MACOCP     = 0xB0;

constexpr uint32_t RDSAR_LOW  = 0xE4;
constexpr uint32_t RDSAR_HIGH = 0xE8;
constexpr uint32_t CPLUS_CMD  = 0xE0;
constexpr uint32_t MISC       = 0xF0;
constexpr uint32_t MISC_CFG   = 0xF2;
constexpr uint32_t RSS_CTRL   = 0x4500;
constexpr uint32_t Q_NUM_CTRL = 0x4800;
}

constexpr uint8_t CR_RST = 0x10;
constexpr uint8_t CR_RE  = 0x08;
constexpr uint8_t CR_TE  = 0x04;

#pragma pack(push, 1)
struct tx_desc
{
    volatile uint32_t opts1;
    volatile uint32_t opts2;
    uint64_t          buf_phys;
};
#pragma pack(pop)
static_assert(sizeof(tx_desc) == 16, "tx_desc must be 16 bytes");

constexpr uint32_t TX_RING_LEN = 32;
constexpr uint32_t TX_BUF_SIZE = 2048;

constexpr uint32_t TX_DESC_OWN = 1u << 31;
constexpr uint32_t TX_DESC_EOR = 1u << 30;
constexpr uint32_t TX_DESC_FS  = 1u << 29;
constexpr uint32_t TX_DESC_LS  = 1u << 28;

struct handle
{
    volatile uint8_t* bar2;
    uint16_t          device_id;
    uint8_t           mac[6];
    bool              link_up;
};

struct ring
{
    tx_desc* desc;
    uint8_t* bufs;
    uint64_t desc_pa;
    uint64_t bufs_pa;
    uint32_t producer;
    uint32_t consumer;
    uint32_t drops;
};

bool discover(const pci::device& dev, handle* out);

bool init(handle* h);

bool tx_ring_init(handle* h, ring* r, void* efi_bs = nullptr);

int tx_enqueue(handle* h, ring* r, const uint8_t* frame, size_t len);

void tx_reap(ring* r);

}