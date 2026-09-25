

#include "hv_platform.h"

namespace
{

volatile bool g_boot_time       = false;
void* volatile g_hvb_base       = nullptr;
void* volatile g_system_table   = nullptr;
void* volatile g_debug_log_file = nullptr;
}

namespace hv_platform
{
bool is_boot_time()
{
    return g_boot_time;
}

void enter_boot_time(void* hvb_base, void* system_table)
{
    g_hvb_base     = hvb_base;
    g_system_table = system_table;
    g_boot_time    = true;
}

void leave_boot_time()
{
    g_boot_time      = false;
    g_hvb_base       = nullptr;
    g_system_table   = nullptr;
    g_debug_log_file = nullptr;
}

void detach_system_table()
{
    g_system_table = nullptr;
}

void attach_debug_log(void* efi_file_protocol)
{
    g_debug_log_file = efi_file_protocol;
}
void detach_debug_log()
{
    g_debug_log_file = nullptr;
}
void* debug_log_file()
{
    return g_debug_log_file;
}

void* hvb_base()
{
    return g_hvb_base;
}
void* system_table()
{
    return g_system_table;
}
}
