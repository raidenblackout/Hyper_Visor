#include "MicroVM.h"
#include "mvm_client.h"

namespace mvm
{

MicroVM::MicroVM(Hypervisor& hv, size_t memory_size) : m_hv(&hv), m_handle(0)
{
    m_handle = microvm_create(static_cast<uint64_t>(memory_size));
}

MicroVM::~MicroVM()
{
    if (m_handle != 0)
    {
        microvm_destroy(m_handle);
    }
}

MicroVM::MicroVM(MicroVM&& other) noexcept : m_hv(other.m_hv), m_handle(other.m_handle)
{
    other.m_handle = 0;
}

MicroVM& MicroVM::operator=(MicroVM&& other) noexcept
{
    if (this != &other)
    {
        if (m_handle != 0)
        {
            microvm_destroy(m_handle);
        }
        m_hv           = other.m_hv;
        m_handle       = other.m_handle;
        other.m_handle = 0;
    }
    return *this;
}

bool MicroVM::IsValid() const
{
    return m_handle != 0;
}

bool MicroVM::LoadPayload(
    const void* payload_data, size_t payload_size, size_t stack_size, uint64_t flags, uint8_t xor_key)
{
    if (!IsValid())
        return false;
    return microvm_load(
        m_handle, payload_data, static_cast<uint64_t>(payload_size), static_cast<uint64_t>(stack_size), flags, xor_key);
}

bool MicroVM::MapMailbox(uint64_t mailbox_pa)
{
    if (!IsValid())
        return false;
    return microvm_map_mailbox(m_handle, mailbox_pa);
}

int64_t MicroVM::Run(size_t max_steps)
{
    if (!IsValid())
        return -1;

    for (size_t i = 0; i < max_steps; ++i)
    {
        int64_t result = microvm_step(m_handle);
        if (result != 0)
            return result;
    }
    return 0;
}

int64_t MicroVM::Step()
{
    if (!IsValid())
        return -1;
    return microvm_step(m_handle);
}

}
