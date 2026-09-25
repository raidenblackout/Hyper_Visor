#include "Hypervisor.h"
#include "mvm_client.h"

namespace mvm
{

bool Hypervisor::Connect()
{
    return hypervisor_present();
}

bool Hypervisor::RequestShutdown()
{
    return request_shutdown();
}

bool Hypervisor::Vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result)
{
    return do_vmmcall(reason, p1, p2, p3, out_result);
}

}
