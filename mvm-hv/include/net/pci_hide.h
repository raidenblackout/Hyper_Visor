
#pragma once
#include <cstdint>
#include "common.h"

namespace pci_hide
{

bool add_region(uint64_t gpa);

bool install_carveouts(ROOT_CONTEXT* root);

bool is_hidden(uint64_t gpa);

void emulate(HOST_CONTEXT* ctx, uint64_t fault_gpa);

struct stats
{
    uint64_t emulated;
    uint64_t decode_failures;
};
stats get_stats();

}
