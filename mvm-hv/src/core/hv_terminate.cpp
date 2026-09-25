
#include "hv_terminate.h"

extern "C" volatile uint32_t g_halt_reason[64] = {};
extern "C" volatile uint64_t g_halt_tsc[64]    = {};
extern "C" volatile uint64_t g_halt_rip[64]    = {};
