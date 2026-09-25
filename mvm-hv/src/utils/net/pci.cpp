
#include "net/pci.h"
#include "boot_stubs.h"

namespace pci
{

static volatile uint32_t* ecam_reg(uint64_t base, uint16_t bdf, uint32_t off)
{
    const uint64_t bus = (bdf >> 8) & 0xFF;
    const uint64_t dev = (bdf >> 3) & 0x1F;
    const uint64_t fn  = bdf & 0x07;
    return reinterpret_cast<volatile uint32_t*>(base + (bus << 20) + (dev << 15) + (fn << 12) + off);
}

bool probe(uint64_t mcfg_base_pa, uint16_t bdf, device* out)
{
    if (!out || !mcfg_base_pa)
        return false;

    uint32_t v = *ecam_reg(mcfg_base_pa, bdf, 0x00);
    if (v == 0xFFFFFFFFu)
        return false;
    out->vendor_id = (uint16_t)(v & 0xFFFF);
    out->device_id = (uint16_t)(v >> 16);

    for (uint32_t i = 0; i < 6; ++i)
    {
        out->bar[i]      = 0;
        out->bar_size[i] = 0;

        volatile uint32_t* reg  = ecam_reg(mcfg_base_pa, bdf, 0x10 + i * 4);
        uint32_t           orig = *reg;
        if (orig == 0)
            continue;
        if (orig & 0x1)
            continue;

        uint32_t type     = (orig >> 1) & 0x3;
        uint64_t base     = orig & 0xFFFFFFF0u;
        bool     is_64bit = (type == 0x2);
        if (is_64bit && i + 1 < 6)
        {
            volatile uint32_t* hi  = ecam_reg(mcfg_base_pa, bdf, 0x10 + (i + 1) * 4);
            base                  |= (uint64_t)(*hi) << 32;
        }
        out->bar[i] = base;

        *reg             = 0xFFFFFFFFu;
        uint32_t mask    = *reg;
        *reg             = orig;
        out->bar_size[i] = (~(mask & 0xFFFFFFF0u)) + 1;

        if (is_64bit && i + 1 < 6)
        {
            ++i;
            out->bar[i]      = 0;
            out->bar_size[i] = 0;
        }
    }
    return true;
}

void enable_bus_master(uint64_t mcfg_base_pa, uint16_t bdf)
{
    volatile uint16_t* cmd = reinterpret_cast<volatile uint16_t*>(ecam_reg(mcfg_base_pa, bdf, 0x04));
    uint16_t           v   = *cmd;
    constexpr uint16_t BME = 0x04;
    constexpr uint16_t MSE = 0x02;
    *cmd                   = v | BME | MSE;
}

void log_command_reg(uint64_t mcfg_base_pa, uint16_t bdf)
{
    volatile uint16_t* cmd = reinterpret_cast<volatile uint16_t*>(ecam_reg(mcfg_base_pa, bdf, 0x04));
    boot_stubs::log("pci_cmd: bdf=%02x:%02x.%x cmd=0x%04x (BME=%u MSE=%u)",
                    (unsigned)((bdf >> 8) & 0xFF),
                    (unsigned)((bdf >> 3) & 0x1F),
                    (unsigned)(bdf & 0x07),
                    (unsigned)*cmd,
                    (unsigned)((*cmd >> 2) & 1),
                    (unsigned)((*cmd >> 1) & 1));
}

void disable_iommu_if_present(uint64_t mcfg_base_pa)
{
    constexpr uint16_t IOMMU_BDF = 0x0002;

    uint32_t id = *ecam_reg(mcfg_base_pa, IOMMU_BDF, 0x00);
    if (id == 0xFFFFFFFFu || (id & 0xFFFF) != 0x1022)
    {
        boot_stubs::log("iommu: no AMD IOMMU at 00:00.2 (id=0x%08x)", id);
        return;
    }
    boot_stubs::log("iommu: found AMD dev=0x%04x at 00:00.2", (unsigned)(id >> 16));

    uint32_t cap_ptr       = *ecam_reg(mcfg_base_pa, IOMMU_BDF, 0x34) & 0xFF;
    uint64_t iommu_base    = 0;
    bool     iommu_enabled = false;

    for (int walk = 0; walk < 48 && cap_ptr >= 0x40; ++walk)
    {
        uint32_t hdr      = *ecam_reg(mcfg_base_pa, IOMMU_BDF, cap_ptr);
        uint8_t  cap_id   = (uint8_t)(hdr & 0xFF);
        uint8_t  next_ptr = (uint8_t)((hdr >> 8) & 0xFF);

        if (cap_id == 0x0F)
        {
            uint32_t base_lo = *ecam_reg(mcfg_base_pa, IOMMU_BDF, cap_ptr + 4);
            uint32_t base_hi = *ecam_reg(mcfg_base_pa, IOMMU_BDF, cap_ptr + 8);
            iommu_enabled    = (base_lo & 1) != 0;
            iommu_base       = ((uint64_t)base_hi << 32) | (base_lo & ~0x3FFFull);
            boot_stubs::log("iommu: cap at 0x%02x base=0x%llx en=%u",
                            (unsigned)cap_ptr,
                            (unsigned long long)iommu_base,
                            (unsigned)iommu_enabled);
            break;
        }
        cap_ptr = next_ptr;
    }

    if (!iommu_base)
    {
        boot_stubs::log("iommu: capability 0x0F not found in chain");
        return;
    }

    if (!iommu_enabled)
    {
        boot_stubs::log("iommu: not enabled -- DMA should be unrestricted");
        return;
    }

    volatile uint64_t* ctrl    = reinterpret_cast<volatile uint64_t*>(iommu_base + 0x18);
    uint64_t           ctl_val = *ctrl;
    boot_stubs::log("iommu: MMIO ctrl=0x%llx IommuEn=%u", (unsigned long long)ctl_val, (unsigned)(ctl_val & 1));

    if (ctl_val & 1)
    {
        *ctrl           = ctl_val & ~1ull;
        uint64_t ctl_rb = *ctrl;
        boot_stubs::log("iommu: DISABLED (ctrl now=0x%llx)", (unsigned long long)ctl_rb);
    }
}

void ensure_d0(uint64_t mcfg_base_pa, uint16_t bdf)
{
    uint32_t cap_ptr = *ecam_reg(mcfg_base_pa, bdf, 0x34) & 0xFF;

    for (int walk = 0; walk < 48 && cap_ptr >= 0x40; ++walk)
    {
        uint32_t hdr      = *ecam_reg(mcfg_base_pa, bdf, cap_ptr);
        uint8_t  cap_id   = (uint8_t)(hdr & 0xFF);
        uint8_t  next_ptr = (uint8_t)((hdr >> 8) & 0xFF);

        if (cap_id == 0x01)
        {
            volatile uint32_t* pmcsr = ecam_reg(mcfg_base_pa, bdf, cap_ptr + 4);
            uint32_t           v     = *pmcsr;
            uint32_t           state = v & 0x3;
            boot_stubs::log("pci_pm: D%u (PMCSR=0x%08x cap@0x%02x)", state, v, (unsigned)cap_ptr);
            if (state != 0)
            {
                *pmcsr = v & ~0x3u;
                for (volatile uint32_t i = 0; i < 10000000; ++i)
                {
                }
                uint32_t rb = *ecam_reg(mcfg_base_pa, bdf, cap_ptr + 4);
                boot_stubs::log("pci_pm: -> D0 (PMCSR=0x%08x)", rb);
            }
            return;
        }
        cap_ptr = next_ptr;
    }
    boot_stubs::log("pci_pm: no PM cap found");
}

void disable_aspm(uint64_t mcfg_base_pa, uint16_t bdf)
{
    uint32_t cap_ptr = *ecam_reg(mcfg_base_pa, bdf, 0x34) & 0xFF;

    for (int walk = 0; walk < 48 && cap_ptr >= 0x40; ++walk)
    {
        uint32_t hdr      = *ecam_reg(mcfg_base_pa, bdf, cap_ptr);
        uint8_t  cap_id   = (uint8_t)(hdr & 0xFF);
        uint8_t  next_ptr = (uint8_t)((hdr >> 8) & 0xFF);

        if (cap_id == 0x10)
        {
            volatile uint16_t* lnkctl =
                reinterpret_cast<volatile uint16_t*>(ecam_reg(mcfg_base_pa, bdf, cap_ptr + 0x10));
            uint16_t v    = *lnkctl;
            uint16_t aspm = v & 0x3;
            boot_stubs::log("pci_aspm: LnkCtl=0x%04x ASPM=%u", (unsigned)v, (unsigned)aspm);
            if (aspm)
            {
                *lnkctl     = v & ~(uint16_t)0x3;
                uint16_t rb = *reinterpret_cast<volatile uint16_t*>(ecam_reg(mcfg_base_pa, bdf, cap_ptr + 0x10));
                boot_stubs::log("pci_aspm: disabled (LnkCtl=0x%04x)", (unsigned)rb);
            }
            return;
        }
        cap_ptr = next_ptr;
    }
    boot_stubs::log("pci_aspm: no PCIe cap found");
}

}