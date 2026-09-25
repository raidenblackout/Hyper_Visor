
#include "net/net_log.h"
#include "net/pci.h"
#include "net/rtl8125.h"
#include "../../../bootloader/net/udp_frame.h"
#include "boot_stubs.h"
#include "hvmem/hvmem.h"
#include <intrin.h>

namespace net_log
{

namespace
{
constexpr uint32_t LINE_RING_LEN = 128;
constexpr uint32_t LINE_MAX      = 248;

struct slot_t
{
    volatile uint32_t len;
    volatile uint32_t seq;
    uint8_t           bytes[LINE_MAX];
};
static_assert(sizeof(slot_t) == 256, "slot_t must be 256 bytes");

slot_t*            g_ring        = nullptr;
volatile uint64_t  g_head        = 0;
volatile uint64_t  g_tail        = 0;
rtl8125::handle    g_nic         = {};
rtl8125::ring      g_tx          = {};
netlog_cfg_v4      g_cfg         = {};
uint8_t            g_gw_mac[6]   = {};
bool               g_ready       = false;
stats              g_stats       = {};
constexpr uint16_t g_src_port_be = 0x8515;

volatile bool g_enabled = false;

static volatile long     g_flush_lock           = 0;
static volatile uint64_t g_flush_lock_owner_tsc = 0;
static volatile uint32_t g_flush_stall          = 0;

static constexpr uint64_t LOCK_TIMEOUT_TSC = 30ull * 1000ull * 1000ull;

static bool try_lock_flush()
{
    const uint64_t start = __rdtsc();
    while (_InterlockedExchange(&g_flush_lock, 1) != 0)
    {
        if ((__rdtsc() - start) > LOCK_TIMEOUT_TSC)
        {
            (void)_InterlockedIncrement(reinterpret_cast<volatile long*>(&g_flush_stall));
            return false;
        }
        _mm_pause();
    }
    g_flush_lock_owner_tsc = __rdtsc();
    return true;
}
static void unlock_flush()
{
    g_flush_lock_owner_tsc = 0;
    _InterlockedExchange(&g_flush_lock, 0);
}
}

bool is_ready()
{
    return g_ready;
}
bool is_enabled()
{
    return g_enabled;
}

void set_enabled(bool on)
{
    g_enabled = on;
}

stats get_stats()
{
    g_stats.link_up     = g_nic.link_up ? 1 : 0;
    g_stats.flush_stall = g_flush_stall;
    return g_stats;
}

bool init(const netlog_cfg_v4& cfg, void* efi_bs)
{
    if (g_ready)
        return true;
    if (!cfg.enable)
        return false;
    if (cfg.mode == NETLOG_MODE_BOOT_ONLY)
        return false;
    if (!cfg.mcfg_base_pa || !cfg.hv_bdf)
        return false;

    g_cfg = cfg;
    for (int i = 0; i < 6; ++i)
        g_gw_mac[i] = cfg.gateway_mac[i];

    pci::device dev = {};
    if (!pci::probe(cfg.mcfg_base_pa, cfg.hv_bdf, &dev))
        return false;
    pci::enable_bus_master(cfg.mcfg_base_pa, cfg.hv_bdf);
    pci::log_command_reg(cfg.mcfg_base_pa, cfg.hv_bdf);
    pci::disable_iommu_if_present(cfg.mcfg_base_pa);
    pci::ensure_d0(cfg.mcfg_base_pa, cfg.hv_bdf);
    pci::disable_aspm(cfg.mcfg_base_pa, cfg.hv_bdf);
    if (!rtl8125::discover(dev, &g_nic))
        return false;
    if (!rtl8125::init(&g_nic))
        return false;
    if (!rtl8125::tx_ring_init(&g_nic, &g_tx, efi_bs))
        return false;

    g_ring = static_cast<slot_t*>(hvmem::allocate(sizeof(slot_t) * LINE_RING_LEN, hvmem::PAGE_BYTES));
    if (!g_ring)
        return false;
    for (uint32_t i = 0; i < LINE_RING_LEN; ++i)
    {
        g_ring[i].len = 0;
        g_ring[i].seq = 0;
    }

    g_ready   = true;
    g_enabled = true;
    g_head    = 0;
    g_tail    = 0;
    g_stats   = {};
    boot_stubs::log("net_log: ready (ring at %p link=%s)", g_ring, g_nic.link_up ? "up" : "down");

    {
        static const char probe[] = "net_log: TX_PROBE ping";
        enqueue(probe, sizeof(probe) - 1);
        flush();

        volatile uint32_t* own_ptr      = reinterpret_cast<volatile uint32_t*>(&g_tx.desc[0].opts1);
        bool               nic_consumed = false;
        for (uint32_t spin = 0; spin < 2000000; ++spin)
        {
            if (!(*own_ptr & rtl8125::TX_DESC_OWN))
            {
                nic_consumed = true;
                break;
            }
        }
        uint32_t isr_rb = *reinterpret_cast<volatile uint32_t*>(&g_nic.bar2[rtl8125::reg::ISR0]);
        boot_stubs::log("net_log: probe enq=%llu tx=%llu drop=%llu OWN=%s ISR=0x%08x",
                        (unsigned long long)g_stats.enq_ok,
                        (unsigned long long)g_stats.tx_ok,
                        (unsigned long long)g_stats.tx_drop,
                        nic_consumed ? "cleared(NIC-sent)" : "STUCK(NIC-dead)",
                        isr_rb);
    }
    return true;
}

void enqueue(const char* line, size_t len)
{
    if (!g_ready || !g_enabled || !line || len == 0)
        return;
    if (len > LINE_MAX)
        len = LINE_MAX;

    if ((g_head - g_tail) >= LINE_RING_LEN)
    {
        ++g_stats.enq_drop;
        return;
    }

    uint64_t slot_idx = (uint64_t)_InterlockedIncrement64((volatile long long*)&g_head) - 1;

    slot_t& s = g_ring[slot_idx % LINE_RING_LEN];
    for (size_t i = 0; i < len; ++i)
        s.bytes[i] = (uint8_t)line[i];

    _mm_sfence();
    s.len = (uint32_t)len;
    s.seq = (uint32_t)(slot_idx + 1);
    ++g_stats.enq_ok;
}

void flush()
{
    if (!g_ready || !g_enabled)
        return;

    if (!try_lock_flush())
        return;

    uint64_t head = g_head;
    uint64_t tail = g_tail;

    constexpr uint32_t MAX_PER_FLUSH = 8;
    uint32_t           sent          = 0;

    while (tail < head && sent < MAX_PER_FLUSH)
    {
        slot_t&  s            = g_ring[tail % LINE_RING_LEN];
        uint32_t expected_seq = (uint32_t)(tail + 1);
        if (s.seq != expected_seq)
        {
            break;
        }
        uint32_t len = s.len;
        if (len > LINE_MAX)
        {
            ++tail;
            ++g_stats.enq_drop;
            continue;
        }

        uint8_t payload[LINE_MAX + 2];
        for (uint32_t i = 0; i < len; ++i)
            payload[i] = s.bytes[i];
        payload[len]        = '\n';
        const uint32_t plen = len + 1;

        uint8_t frame[UDP_FRAME_MAX_PAYLOAD + 64];
        size_t  flen = udp_frame_build(frame,
                                       sizeof(frame),
                                       g_nic.mac,
                                       g_gw_mac,
                                       g_cfg.src_ip_be,
                                       g_cfg.dest_ip_be,
                                       g_src_port_be,
                                       g_cfg.dest_port_be,
                                       payload,
                                       plen);
        if (!flen)
        {
            ++tail;
            ++g_stats.tx_drop;
            continue;
        }

        if (rtl8125::tx_enqueue(&g_nic, &g_tx, frame, flen) != 0)
        {
            break;
        }

        ++g_stats.tx_ok;
        ++sent;
        ++tail;
    }
    g_tail = tail;

    unlock_flush();
}

}