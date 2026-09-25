#pragma once
#include <cstdint>

namespace mvm
{

class Hypervisor
{
  public:
    bool Connect();

    bool RequestShutdown();

    bool Vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result);
};

}
