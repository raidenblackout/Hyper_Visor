#pragma once
#include <windows.h>
#include <cstdint>
#include "mvm_shared.h"
#include "mvm_client.h"

bool check_hypervisor_present();

bool do_vmmcall(uint64_t reason, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t* out_result);
