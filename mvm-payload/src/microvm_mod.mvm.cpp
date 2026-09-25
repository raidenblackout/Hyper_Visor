#include "microvm_rt.h"

namespace mvm
{

static bool rt_streqi_wide(uint64_t cr3, uint64_t wide_va, const char* ascii, uint32_t max_chars)
{
    if (!wide_va || !ascii)
        return false;

    for (uint32_t i = 0; i < max_chars; ++i)
    {
        char ac = ascii[i];
        if (ac == '\0')
            return false;

        uint16_t wc = ReadValue<uint16_t>(cr3, wide_va + i * 2);
        if (wc >= 'A' && wc <= 'Z')
            wc += 32;
        if (ac >= 'A' && ac <= 'Z')
            ac += 32;
        if (wc > 0x7F || static_cast<char>(wc) != ac)
            return false;
    }
    return ascii[max_chars] == '\0';
}

static bool rt_streq_va(uint64_t cr3, uint64_t va, const char* target)
{
    if (!va || !target)
        return false;

    uint32_t i = 0;
    for (; target[i]; ++i)
    {
        char c = ReadValue<char>(cr3, va + i);
        char t = target[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (t >= 'A' && t <= 'Z')
            t += 32;
        if (c != t)
            return false;
    }
    return ReadValue<char>(cr3, va + i) == '\0';
}

constexpr uint64_t PEB_LDR           = 0x18;
constexpr uint64_t LDR_IN_LOAD_ORDER = 0x10;
constexpr uint64_t LDR_DLLBASE       = 0x30;
constexpr uint64_t LDR_SIZE_OF_IMAGE = 0x40;
constexpr uint64_t LDR_BASE_DLL_NAME = 0x58;

struct LdrEntry
{
    uint64_t base;
    uint32_t size;
    uint64_t name_buf;
    uint16_t name_len;
};

static bool walk_ldr(uint64_t cr3, const char* name, LdrEntry* out)
{
    uint64_t peb = GetProcessPeb(cr3);
    if (!peb)
        return false;

    uint64_t ldr = ReadValue<uint64_t>(cr3, peb + PEB_LDR);
    if (!ldr)
        return false;

    uint64_t list_head = ldr + LDR_IN_LOAD_ORDER;
    uint64_t flink     = ReadValue<uint64_t>(cr3, list_head);

    for (uint32_t i = 0; i < 256 && flink && flink != list_head; ++i)
    {
        uint64_t entry = flink;

        uint64_t base = ReadValue<uint64_t>(cr3, entry + LDR_DLLBASE);
        uint32_t size = ReadValue<uint32_t>(cr3, entry + LDR_SIZE_OF_IMAGE);

        uint16_t name_len = ReadValue<uint16_t>(cr3, entry + LDR_BASE_DLL_NAME);
        uint64_t name_buf = ReadValue<uint64_t>(cr3, entry + LDR_BASE_DLL_NAME + 8);

        if (name == nullptr || (name_buf && rt_streqi_wide(cr3, name_buf, name, name_len / 2)))
        {
            if (out)
            {
                out->base     = base;
                out->size     = size;
                out->name_buf = name_buf;
                out->name_len = name_len;
            }
            return true;
        }

        flink = ReadValue<uint64_t>(cr3, flink);
    }
    return false;
}

uint64_t FindModuleBase(uint64_t cr3, const char* name)
{
    LdrEntry entry{};
    return walk_ldr(cr3, name, &entry) ? entry.base : 0;
}

uint64_t FindModuleSize(uint64_t cr3, const char* name)
{
    LdrEntry entry{};
    return walk_ldr(cr3, name, &entry) ? entry.size : 0;
}

uint64_t FindExport(uint64_t cr3, uint64_t module_base, const char* func_name)
{
    uint32_t e_lfanew = ReadValue<uint32_t>(cr3, module_base + 0x3C);
    uint64_t pe       = module_base + e_lfanew;

    uint32_t export_rva = ReadValue<uint32_t>(cr3, pe + 0x88);
    if (!export_rva)
        return 0;

    uint64_t export_dir = module_base + export_rva;

    uint32_t num_names     = ReadValue<uint32_t>(cr3, export_dir + 0x18);
    uint32_t names_rva     = ReadValue<uint32_t>(cr3, export_dir + 0x20);
    uint32_t ordinals_rva  = ReadValue<uint32_t>(cr3, export_dir + 0x24);
    uint32_t functions_rva = ReadValue<uint32_t>(cr3, export_dir + 0x1C);

    for (uint32_t i = 0; i < num_names; ++i)
    {
        uint32_t name_rva = ReadValue<uint32_t>(cr3, module_base + names_rva + i * 4);
        if (rt_streq_va(cr3, module_base + name_rva, func_name))
        {
            uint16_t ordinal  = ReadValue<uint16_t>(cr3, module_base + ordinals_rva + i * 2);
            uint32_t func_rva = ReadValue<uint32_t>(cr3, module_base + functions_rva + ordinal * 4);
            return module_base + func_rva;
        }
    }
    return 0;
}

}
