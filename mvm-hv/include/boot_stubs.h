

#pragma once

#include <stdarg.h>

namespace boot_stubs
{

void log(const char* fmt, ...);
void log_v(const char* fmt, va_list ap);

void set_firmware_sinks(bool enabled);
bool firmware_sinks_enabled();

void log_mem(const char* fmt, ...);
void log_prefix_v(const char* prefix, const char* fmt, va_list ap);

void set_log_mem_enabled(bool on);
bool log_mem_enabled();

[[noreturn]] void halt(const char* msg);
}
