

#pragma once

#include <stdint.h>
#include <stdarg.h>

#include "hv_toggles.h"

namespace fb_panic
{

constexpr uint32_t kSlotBarHide   = 0;
constexpr uint32_t kSlotNetLog    = 1;
constexpr uint32_t kSlotSelfVirt  = 2;
constexpr uint32_t kSlotChainload = 3;

constexpr uint32_t kSlotCpuStatusBase = 4;

#if HV_FB_LOG_ENABLE

void init(const void* hvb_header);

bool available();

const char* status();

bool is_enabled();
void set_enabled(bool on);

void report(const char* line);

void reportf(const char* fmt, ...);

void markf(uint32_t slot, const char* fmt, ...);

void statusf(uint32_t slot, const char* fmt, ...);

void restore_if_wiped();

#else

inline void init(const void*) {}
inline bool available()
{
    return false;
}
inline const char* status()
{
    return "fb_panic: HV_FB_LOG_ENABLE=0";
}
inline bool is_enabled()
{
    return false;
}
inline void set_enabled(bool) {}
inline void report(const char*) {}
inline void reportf(const char*, ...) {}
inline void markf(uint32_t, const char*, ...) {}
inline void statusf(uint32_t, const char*, ...) {}
inline void restore_if_wiped() {}

#endif
}
