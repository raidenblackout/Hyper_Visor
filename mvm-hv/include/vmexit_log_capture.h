#pragma once

#include <cstdint>

struct HOST_CONTEXT;

void vmexit_log_maybe_capture(HOST_CONTEXT* ctx, uint32_t procNum, uint64_t code, uint64_t guest_rcx);
