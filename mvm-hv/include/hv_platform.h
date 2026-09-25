

#pragma once

#include <stdint.h>

namespace hv_platform
{

bool is_boot_time();

void enter_boot_time(void* hvb_base, void* system_table = nullptr);

void leave_boot_time();

void detach_system_table();

void* hvb_base();

void* system_table();

void  attach_debug_log(void* efi_file_protocol);
void  detach_debug_log();
void* debug_log_file();
}
