#pragma once
#include "Hypervisor.h"
#include <cstdint>

namespace mvm
{

class MicroVM
{
  public:
    MicroVM(Hypervisor& hv, size_t memory_size);
    ~MicroVM();

    MicroVM(const MicroVM&)            = delete;
    MicroVM& operator=(const MicroVM&) = delete;

    MicroVM(MicroVM&& other) noexcept;
    MicroVM& operator=(MicroVM&& other) noexcept;

    bool IsValid() const;

    bool LoadPayload(const void* payload_data,
                     size_t      payload_size,
                     size_t      stack_size = 0,
                     uint64_t    flags      = 0,
                     uint8_t     xor_key    = 0x7A);

    bool MapMailbox(uint64_t mailbox_pa);

    int64_t Run(size_t max_steps = 1000000);

    int64_t Step();

    uint64_t Handle() const
    {
        return m_handle;
    }

  private:
    Hypervisor* m_hv;
    uint64_t    m_handle;
};

}
