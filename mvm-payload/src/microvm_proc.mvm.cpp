#include "microvm_rt.h"

namespace mvm
{

extern MicroVmConfig* g_config;

static bool rt_streqi_proc(const char* img_name, const char* target)
{
    if (!img_name || !target)
        return false;

    auto to_lower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; };

    uint32_t i = 0;
    for (; i < 15; ++i)
    {
        char ca = to_lower(img_name[i]);
        char cb = to_lower(target[i]);

        if (cb == '\0')
        {
            if (ca == '\0')
                return true;

            if (ca == '.' && to_lower(img_name[i + 1]) == 'e' && to_lower(img_name[i + 2]) == 'x' &&
                to_lower(img_name[i + 3]) == 'e' && (i + 4 >= 15 || img_name[i + 4] == '\0'))
            {
                return true;
            }
            return false;
        }

        if (ca == '\0')
        {
            if (cb == '.' && to_lower(target[i + 1]) == 'e' && to_lower(target[i + 2]) == 'x' &&
                to_lower(target[i + 3]) == 'e' && target[i + 4] == '\0')
            {
                return true;
            }
            return false;
        }

        if (ca != cb)
            return false;
    }

    if (target[15] == '\0')
        return true;

    if (target[15] == '.' && to_lower(target[16]) == 'e' && to_lower(target[17]) == 'x' &&
        to_lower(target[18]) == 'e' && target[19] == '\0')
    {
        return true;
    }

    return true;
}

template <typename Fn> static uint64_t walk_eprocess_list(Fn match_fn)
{
    if (!g_config || !g_config->eprocess_list_head_va || !g_config->kernel_cr3)
        return 0;

    uint64_t kcr3       = g_config->kernel_cr3;
    uint64_t anchor_va  = g_config->eprocess_list_head_va;
    uint64_t current_va = anchor_va;

    for (uint32_t i = 0; i < 4096; ++i)
    {
        if (match_fn(kcr3, current_va))
        {
            uint64_t dtb = ReadValue<uint64_t>(kcr3, current_va + g_config->eprocess_dtb);
            return dtb & PFN_MASK;
        }

        uint64_t flink_va = ReadValue<uint64_t>(kcr3, current_va + g_config->eprocess_links);
        if (!flink_va)
            return 0;

        current_va = flink_va - g_config->eprocess_links;
        if (current_va == anchor_va)
            break;
    }
    return 0;
}

uint64_t FindProcessByPid(uint32_t pid)
{
    uint64_t found_cr3 = 0;
    walk_eprocess_list(
        [pid, &found_cr3](uint64_t kcr3, uint64_t eprocess_va)
        {
            uint32_t p = static_cast<uint32_t>(ReadValue<uint64_t>(kcr3, eprocess_va + g_config->eprocess_pid));
            if (p != pid)
                return false;
            uint64_t dtb = ReadValue<uint64_t>(kcr3, eprocess_va + g_config->eprocess_dtb);
            uint64_t cr3 = dtb & PFN_MASK;
            if (!cr3)
                return false;
            found_cr3 = cr3;
            return true;
        });
    return found_cr3;
}

static bool is_valid_process(uint64_t kcr3, uint64_t eprocess_va, uint64_t& out_cr3)
{
    uint64_t dtb = ReadValue<uint64_t>(kcr3, eprocess_va + g_config->eprocess_dtb);
    uint64_t cr3 = dtb & PFN_MASK;
    if (!cr3)
        return false;

    uint64_t peb = ReadValue<uint64_t>(kcr3, eprocess_va + g_config->eprocess_peb);
    if (!peb)
        return false;

    uint64_t ldr = ReadValue<uint64_t>(cr3, peb + 0x18);
    if (!ldr)
        return false;

    out_cr3 = cr3;
    return true;
}

uint64_t FindFirstProcessByName(const char* name)
{
    uint64_t found_cr3 = 0;
    walk_eprocess_list(
        [name, &found_cr3](uint64_t kcr3, uint64_t eprocess_va)
        {
            char img_name[16] = {};
            ReadVirtual(kcr3, eprocess_va + g_config->eprocess_name, img_name, 15);
            if (rt_streqi_proc(img_name, name))
            {
                uint64_t cr3 = 0;
                if (is_valid_process(kcr3, eprocess_va, cr3))
                {
                    found_cr3 = cr3;
                    return true;
                }
            }
            return false;
        });
    return found_cr3;
}

uint64_t FindProcessByName(const char* name)
{
    return FindFirstProcessByName(name);
}

uint64_t FindLastProcessByName(const char* name)
{
    uint64_t last_cr3 = 0;
    walk_eprocess_list(
        [name, &last_cr3](uint64_t kcr3, uint64_t eprocess_va)
        {
            char img_name[16] = {};
            ReadVirtual(kcr3, eprocess_va + g_config->eprocess_name, img_name, 15);
            if (rt_streqi_proc(img_name, name))
            {
                uint64_t cr3 = 0;
                if (is_valid_process(kcr3, eprocess_va, cr3))
                {
                    last_cr3 = cr3;
                }
            }
            return false;
        });
    return last_cr3;
}

uint32_t FindProcessesByName(const char* name, uint64_t* out_cr3s, uint32_t max_count)
{
    if (!out_cr3s || max_count == 0)
        return 0;
    uint32_t count = 0;

    walk_eprocess_list(
        [name, out_cr3s, max_count, &count](uint64_t kcr3, uint64_t eprocess_va)
        {
            char img_name[16] = {};
            ReadVirtual(kcr3, eprocess_va + g_config->eprocess_name, img_name, 15);
            if (rt_streqi_proc(img_name, name))
            {
                uint64_t cr3 = 0;
                if (is_valid_process(kcr3, eprocess_va, cr3))
                {
                    out_cr3s[count++] = cr3;
                    if (count >= max_count)
                        return true;
                }
            }
            return false;
        });

    return count;
}

uint64_t GetProcessPeb(uint64_t cr3)
{
    uint64_t peb_va = 0;
    walk_eprocess_list(
        [cr3, &peb_va](uint64_t kcr3, uint64_t eprocess_va)
        {
            uint64_t dtb = ReadValue<uint64_t>(kcr3, eprocess_va + g_config->eprocess_dtb);
            if ((dtb & PFN_MASK) == cr3)
            {
                peb_va = ReadValue<uint64_t>(kcr3, eprocess_va + g_config->eprocess_peb);
                return true;
            }
            return false;
        });
    return peb_va;
}

uint64_t GetProcessBase(uint64_t cr3)
{
    uint64_t peb_va = GetProcessPeb(cr3);
    if (!peb_va)
        return 0;
    return ReadValue<uint64_t>(cr3, peb_va + 0x10);
}

}
