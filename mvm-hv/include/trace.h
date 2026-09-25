#pragma once

#include <cstdint>

struct HOST_CONTEXT;

void trace_record(HOST_CONTEXT* ctx, uint64_t code);
bool trace_step_consume_db(HOST_CONTEXT* ctx);
bool owns_report_band(uint32_t cpu);

extern "C" void HvPublishWatch(uint32_t slot, uint64_t pa);
