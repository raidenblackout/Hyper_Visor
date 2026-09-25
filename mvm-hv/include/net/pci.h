
#pragma once
#include <cstdint>

namespace pci
{

struct device
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t bar[6];
    uint32_t bar_size[6];
};

bool probe(uint64_t mcfg_base_pa, uint16_t bdf, device* out);

void enable_bus_master(uint64_t mcfg_base_pa, uint16_t bdf);

void log_command_reg(uint64_t mcfg_base_pa, uint16_t bdf);

void disable_iommu_if_present(uint64_t mcfg_base_pa);

void ensure_d0(uint64_t mcfg_base_pa, uint16_t bdf);

void disable_aspm(uint64_t mcfg_base_pa, uint16_t bdf);

}