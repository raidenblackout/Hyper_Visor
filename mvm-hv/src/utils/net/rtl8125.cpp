
#include "net/rtl8125.h"
#include "boot_stubs.h"
#include "hvmem/hvmem.h"
#include "svm/mp_services.h"
#include <intrin.h>

namespace rtl8125
{

bool discover(const pci::device& dev, handle* out)
{
    if (!out)
        return false;
    if (dev.vendor_id != VENDOR_REALTEK)
        return false;
    if (dev.device_id != DEVICE_RTL8125 && dev.device_id != DEVICE_RTL8125_BG)
    {
        boot_stubs::log("rtl8125: unexpected device_id 0x%04x -- unsupported", (unsigned)dev.device_id);
        return false;
    }
    out->device_id = dev.device_id;

    if (dev.bar[2] == 0)
    {
        boot_stubs::log("rtl8125: BAR2 not populated -- cannot drive");
        return false;
    }
    out->bar2 = reinterpret_cast<volatile uint8_t*>(dev.bar[2]);

    for (int i = 0; i < 4; ++i)
        out->mac[i] = out->bar2[reg::IDR0 + i];
    for (int i = 0; i < 2; ++i)
        out->mac[4 + i] = out->bar2[reg::IDR4 + i];
    out->link_up = false;

    boot_stubs::log("rtl8125: discovered dev=0x%04x BAR2=0x%llx MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                    (unsigned)dev.device_id,
                    (unsigned long long)dev.bar[2],
                    out->mac[0],
                    out->mac[1],
                    out->mac[2],
                    out->mac[3],
                    out->mac[4],
                    out->mac[5]);
    return true;
}

static bool poll_clear8(volatile uint8_t* reg, uint8_t bit, uint32_t max)
{
    for (uint32_t i = 0; i < max; ++i)
    {
        if ((*reg & bit) == 0)
            return true;
    }
    return false;
}

static void mac_ocp_write(handle* h, uint16_t reg, uint16_t val)
{
    uint32_t cmd                                                 = 0x80000000u | ((uint32_t)(reg >> 1) << 16) | val;
    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::MACOCP]) = cmd;
}

static uint16_t mac_ocp_read(handle* h, uint16_t reg)
{
    uint32_t cmd                                                 = ((uint32_t)(reg >> 1) << 16);
    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::MACOCP]) = cmd;
    return (uint16_t)*reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::MACOCP]);
}

static void mac_ocp_modify(handle* h, uint16_t reg, uint16_t mask, uint16_t set)
{
    uint16_t v = mac_ocp_read(h, reg);
    mac_ocp_write(h, reg, (uint16_t)((v & (uint16_t)~mask) | set));
}

static void init_8125b_registers(handle* h)
{
    h->bar2[reg::CONFIG2] &= (uint8_t)~0x80;
    h->bar2[reg::CONFIG5] &= (uint8_t)~0x01;
    mac_ocp_modify(h, 0xE092, 0x00FF, 0x0000);

    h->bar2[reg::INT_CFG0] = 0;
    for (uint32_t a = 0xA00; a < 0xA80; a += 4)
        *reinterpret_cast<volatile uint32_t*>(&h->bar2[a]) = 0;
    *reinterpret_cast<volatile uint16_t*>(&h->bar2[reg::INT_CFG1]) = 0;
    mac_ocp_modify(h, 0xEA84, 0x0000, 0x0003);

    h->bar2[reg::CONFIG3] &= (uint8_t)~0x02;

    *reinterpret_cast<volatile uint16_t*>(&h->bar2[0x382])           = 0x221B;
    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::RSS_CTRL])   = 0;
    *reinterpret_cast<volatile uint16_t*>(&h->bar2[reg::Q_NUM_CTRL]) = 0;

    mac_ocp_modify(h, 0xD40A, 0x0010, 0x0000);
    h->bar2[reg::CONFIG1] &= (uint8_t)~0x10;
    mac_ocp_write(h, 0xC140, 0xFFFF);
    mac_ocp_write(h, 0xC142, 0xFFFF);
    mac_ocp_modify(h, 0xD3E2, 0x0FFF, 0x03A9);
    mac_ocp_modify(h, 0xD3E4, 0x00FF, 0x0000);
    mac_ocp_modify(h, 0xE860, 0x0000, 0x0080);
    mac_ocp_modify(h, 0xEB58, 0x0001, 0x0000);
    mac_ocp_modify(h, 0xE614, 0x0700, 0x0200);
    mac_ocp_modify(h, 0xE63E, 0x0C30, 0x0000);
    mac_ocp_modify(h, 0xC0B4, 0x0000, 0x000C);
    mac_ocp_modify(h, 0xEB6A, 0x00FF, 0x0033);
    mac_ocp_modify(h, 0xEB50, 0x03E0, 0x0040);
    mac_ocp_modify(h, 0xE056, 0x00F0, 0x0000);
    mac_ocp_modify(h, 0xE040, 0x1000, 0x0000);
    mac_ocp_modify(h, 0xEA1C, 0x0003, 0x0001);
    mac_ocp_modify(h, 0xEA1C, 0x0004, 0x0000);
    mac_ocp_modify(h, 0xE0C0, 0x4F0F, 0x4403);
    mac_ocp_modify(h, 0xE052, 0x0080, 0x0068);
    mac_ocp_modify(h, 0xD430, 0x0FFF, 0x047F);
    mac_ocp_modify(h, 0xEB54, 0x0000, 0x0001);
    (void)mac_ocp_read(h, 0xEB54);
    mac_ocp_modify(h, 0xEB54, 0x0001, 0x0000);
    *reinterpret_cast<volatile uint16_t*>(&h->bar2[0x1880]) &= (uint16_t)~0x0030;
    mac_ocp_write(h, 0xE098, 0xC302);

    {
        bool ready = false;
        for (uint32_t i = 0; i < 100000; ++i)
        {
            if ((mac_ocp_read(h, 0xE00E) & 0x2000) == 0)
            {
                ready = true;
                break;
            }
        }
        if (!ready)
            boot_stubs::log("rtl8125: OCP 0xe00e ready wait timed out");
    }

    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::MISC]) &= ~(1u << 19);
}

bool init(handle* h)
{
    if (!h || !h->bar2)
        return false;

    h->bar2[reg::CR] = CR_RST;
    if (!poll_clear8(&h->bar2[reg::CR], CR_RST, 1000000))
    {
        boot_stubs::log("rtl8125: reset timeout");
        return false;
    }

    {
        uint8_t pmch       = h->bar2[reg::PMCH];
        h->bar2[reg::PMCH] = pmch | 0xC0;
        boot_stubs::log("rtl8125: PMCH 0x%02x -> 0x%02x", (unsigned)pmch, (unsigned)h->bar2[reg::PMCH]);
    }

    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::IMR0]) = 0;

    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::ISR0]) = 0xFFFFFFFFu;

    h->bar2[reg::MISC_CFG] = h->bar2[reg::MISC_CFG] | 0x40;

    h->bar2[reg::CFG9346] = 0xC0;

    {
        volatile uint16_t* cplus    = reinterpret_cast<volatile uint16_t*>(&h->bar2[reg::CPLUS_CMD]);
        uint16_t           pre      = *cplus;
        constexpr uint16_t PCIDAC   = 1u << 4;
        constexpr uint16_t PCIMulRW = 1u << 3;
        *cplus = (pre | PCIDAC | PCIMulRW) &
                 ~(uint16_t)((1u << 15) | (1u << 14) | (1u << 12) | (1u << 11) | (1u << 10) | (1u << 9) | (1u << 8));
        uint16_t post = *cplus;
        boot_stubs::log("rtl8125: CPlusCmd 0x%04x -> 0x%04x (PCIDAC=%u)",
                        (unsigned)pre,
                        (unsigned)post,
                        (unsigned)((post >> 4) & 1));
    }

    for (int i = 0; i < 4; ++i)
        h->mac[i] = h->bar2[reg::IDR0 + i];
    for (int i = 0; i < 2; ++i)
        h->mac[4 + i] = h->bar2[reg::IDR4 + i];

    init_8125b_registers(h);

    mac_ocp_modify(h, 0xC0AC, 0x0000, 0x1F80);

    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::RX_CONFIG]) = 0;

    h->bar2[reg::CFG9346] = 0x00;

    for (uint32_t poll = 0; poll < 2000; ++poll)
    {
        uint32_t phy = *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::PHY_STATUS]);
        if (phy & 0x2)
        {
            h->link_up = true;
            break;
        }
    }

    boot_stubs::log("rtl8125: init complete link=%s", h->link_up ? "up" : "down");
    return true;
}

typedef uint64_t(EFIAPI* EFI_ALLOCATE_PAGES_t)(uint32_t type, uint32_t memory_type, uint64_t pages, uint64_t* addr);

static void* alloc_below_4g(void* efi_bs, uint64_t bytes)
{
    if (!efi_bs)
        return nullptr;
    auto* bs    = static_cast<EFI_BOOT_SERVICES_MIN_t*>(efi_bs);
    auto  alloc = reinterpret_cast<EFI_ALLOCATE_PAGES_t>(bs->AllocatePages);
    if (!alloc)
        return nullptr;
    uint64_t pages  = (bytes + 4095) / 4096;
    uint64_t addr   = 0xFFFFFFFFull;
    uint64_t status = alloc(1, 0, pages, &addr);
    if (status != 0 || addr > 0xFFFFFFFFull)
        return nullptr;
    auto* p = reinterpret_cast<uint8_t*>(addr);
    for (uint64_t i = 0; i < pages * 4096; ++i)
        p[i] = 0;
    return p;
}

bool tx_ring_init(handle* h, ring* r, void* efi_bs)
{
    if (!h || !r)
        return false;

    uint64_t desc_bytes = TX_RING_LEN * sizeof(tx_desc);
    uint64_t bufs_bytes = TX_RING_LEN * TX_BUF_SIZE;

    void* desc_va = alloc_below_4g(efi_bs, desc_bytes);
    void* bufs_va = alloc_below_4g(efi_bs, bufs_bytes);
    if (!desc_va || !bufs_va)
    {
        boot_stubs::log("rtl8125: sub-4G alloc failed, falling back to hvmem (>4G)");
        desc_va = hvmem::allocate(desc_bytes, hvmem::PAGE_BYTES);
        bufs_va = hvmem::allocate(bufs_bytes, hvmem::PAGE_BYTES);
        if (!desc_va || !bufs_va)
            return false;
    }

    r->desc     = static_cast<tx_desc*>(desc_va);
    r->bufs     = static_cast<uint8_t*>(bufs_va);
    r->desc_pa  = reinterpret_cast<uint64_t>(desc_va);
    r->bufs_pa  = reinterpret_cast<uint64_t>(bufs_va);
    r->producer = 0;
    r->consumer = 0;
    r->drops    = 0;

    for (uint32_t i = 0; i < TX_RING_LEN; ++i)
    {
        r->desc[i].opts1    = (i == TX_RING_LEN - 1) ? TX_DESC_EOR : 0;
        r->desc[i].opts2    = 0;
        r->desc[i].buf_phys = r->bufs_pa + i * TX_BUF_SIZE;
    }

    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::TNPDS_HIGH]) = (uint32_t)(r->desc_pa >> 32);
    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::TNPDS_LOW])  = (uint32_t)(r->desc_pa & 0xFFFFFFFFu);

    {
        void* rx_desc = alloc_below_4g(efi_bs, 4 * sizeof(tx_desc));
        if (!rx_desc)
            rx_desc = hvmem::allocate(4 * sizeof(tx_desc), hvmem::PAGE_BYTES);
        if (rx_desc)
        {
            uint64_t rx_pa = reinterpret_cast<uint64_t>(rx_desc);
            auto*    rxd   = static_cast<tx_desc*>(rx_desc);
            for (int i = 0; i < 4; ++i)
            {
                rxd[i].opts1    = 0;
                rxd[i].opts2    = 0;
                rxd[i].buf_phys = 0;
            }
            rxd[3].opts1                                                     = TX_DESC_EOR;
            *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::RDSAR_HIGH]) = (uint32_t)(rx_pa >> 32);
            *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::RDSAR_LOW])  = (uint32_t)(rx_pa & 0xFFFFFFFFu);
            boot_stubs::log("rtl8125: dummy RX ring at 0x%llx", (unsigned long long)rx_pa);
        }
    }

    (void)h->bar2[reg::CR];

    h->bar2[reg::CR] = CR_TE | CR_RE;

    constexpr uint32_t TX_IFG_96                                    = 3u << 24;
    constexpr uint32_t TX_DMA_UNLIM                                 = 7u << 8;
    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::TX_CONFIG]) = TX_IFG_96 | TX_DMA_UNLIM;

    uint32_t tnpds_lo = *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::TNPDS_LOW]);
    uint32_t tnpds_hi = *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::TNPDS_HIGH]);
    uint8_t  cr_rb    = h->bar2[reg::CR];
    uint32_t txcfg_rb = *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::TX_CONFIG]);
    boot_stubs::log(
        "rtl8125: TX ring at 0x%llx (bufs at 0x%llx)", (unsigned long long)r->desc_pa, (unsigned long long)r->bufs_pa);
    boot_stubs::log(
        "rtl8125: TNPDS readback=0x%x_%08x CR=0x%02x TxCfg=0x%08x", tnpds_hi, tnpds_lo, (unsigned)cr_rb, txcfg_rb);
    return true;
}

int tx_enqueue(handle* h, ring* r, const uint8_t* frame, size_t len)
{
    if (!h || !r || !frame)
        return -1;
    if (len > TX_BUF_SIZE)
    {
        ++r->drops;
        return -1;
    }

    tx_reap(r);

    uint32_t next = (r->producer + 1) % TX_RING_LEN;
    if (next == r->consumer)
    {
        ++r->drops;
        return -1;
    }

    uint8_t* dst = r->bufs + r->producer * TX_BUF_SIZE;
    for (size_t i = 0; i < len; ++i)
        dst[i] = frame[i];

    _mm_sfence();

    uint32_t opts1 = (uint32_t)len | TX_DESC_FS | TX_DESC_LS | TX_DESC_OWN;
    if (r->producer == TX_RING_LEN - 1)
        opts1 |= TX_DESC_EOR;
    r->desc[r->producer].opts1 = opts1;

    _mm_sfence();
    r->producer = next;

    *reinterpret_cast<volatile uint16_t*>(&h->bar2[reg::TPPOLL]) = 0x0001;

    *reinterpret_cast<volatile uint32_t*>(&h->bar2[reg::ISR0]) = 0xFFFFFFFFu;
    return 0;
}

void tx_reap(ring* r)
{
    if (!r)
        return;
    while (r->consumer != r->producer)
    {
        if (r->desc[r->consumer].opts1 & TX_DESC_OWN)
            break;
        r->consumer = (r->consumer + 1) % TX_RING_LEN;
    }
}

}