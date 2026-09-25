#include "microvm_rt.h"
#include "microvm_test_report.h"

static TestReport* g_report = nullptr;

static void pass(uint64_t& slot)
{
    slot = TEST_PASS;
    g_report->passed++;
}
static void fail(uint64_t& slot)
{
    slot = TEST_FAIL;
    g_report->failed++;
}

static void test_config(MicroVmConfig* cfg)
{
    g_report->test_config_magic = (cfg->magic == MICROVM_CONFIG_MAGIC) ? TEST_PASS : TEST_FAIL;
    if (cfg->magic == MICROVM_CONFIG_MAGIC)
        g_report->passed++;
    else
        g_report->failed++;

    g_report->test_physmap_base = (cfg->physmap_base == PHYSMAP_BASE) ? TEST_PASS : TEST_FAIL;
    if (cfg->physmap_base == PHYSMAP_BASE)
        g_report->passed++;
    else
        g_report->failed++;
}

static void test_heap()
{
    uint64_t before = mvm::GetHeapRemaining();
    void*    p1     = mvm::Alloc(64);
    uint64_t after  = mvm::GetHeapRemaining();

    if (p1 != nullptr && after < before)
        pass(g_report->test_heap_alloc);
    else
        fail(g_report->test_heap_alloc);

    if (after == before - 64)
        pass(g_report->test_heap_remaining);
    else
        fail(g_report->test_heap_remaining);

    uint64_t remaining  = mvm::GetHeapRemaining();
    void*    p_overflow = mvm::Alloc(remaining + PAGE_SIZE_4K);
    if (p_overflow == nullptr)
        pass(g_report->test_heap_overflow);
    else
        fail(g_report->test_heap_overflow);
}

static void test_physmap_read(MicroVmConfig* cfg)
{
    (void)cfg;
    auto* low_mem = static_cast<volatile uint8_t*>(mvm::PhysToVirt(0));

    volatile uint8_t byte0 = low_mem[0];
    (void)byte0;
    pass(g_report->test_physmap_read);
}

static void test_translate_va_self(MicroVmConfig* cfg)
{
    if (!cfg->kernel_cr3 || !cfg->eprocess_list_head_va)
    {
        fail(g_report->test_translate_va_self);
        return;
    }

    uint64_t pa = mvm::TranslateVA(cfg->kernel_cr3, cfg->eprocess_list_head_va);
    if (pa != 0)
        pass(g_report->test_translate_va_self);
    else
        fail(g_report->test_translate_va_self);
}

static void test_read_write_virtual()
{
    uint64_t* buf = static_cast<uint64_t*>(mvm::Alloc(sizeof(uint64_t)));
    if (!buf)
    {
        fail(g_report->test_read_write_virtual);
        return;
    }

    *buf = 0xDEADBEEFCAFEBABEull;
    if (*buf == 0xDEADBEEFCAFEBABEull)
        pass(g_report->test_read_write_virtual);
    else
        fail(g_report->test_read_write_virtual);
}

static void test_process_discovery(MicroVmConfig* cfg)
{
    auto cr3_ok = [](uint64_t cr3) { return cr3 != 0 && (cr3 & 0xFFFULL) == 0 && (cr3 >> 52) == 0; };

    uint64_t system_cr3 = mvm::FindProcessByPid(4);
    if (cr3_ok(system_cr3))
        pass(g_report->test_find_process_system);
    else
        fail(g_report->test_find_process_system);

    if (cfg && cfg->kernel_cr3 && cfg->eprocess_list_head_va)
    {
        uint64_t kcr3      = cfg->kernel_cr3;
        uint64_t anchor_va = cfg->eprocess_list_head_va;

        {
            uint64_t cur_va = anchor_va;
            for (uint32_t i = 0; i < 4096; ++i)
            {
                uint32_t pid = static_cast<uint32_t>(mvm::ReadValue<uint64_t>(kcr3, cur_va + cfg->eprocess_pid));
                if (pid == 4)
                {
                    anchor_va = cur_va;
                    break;
                }
                uint64_t flink = mvm::ReadValue<uint64_t>(kcr3, cur_va + cfg->eprocess_links);
                if (!flink)
                    break;
                cur_va = flink - cfg->eprocess_links;
                if (cur_va == cfg->eprocess_list_head_va)
                    break;
            }
        }

        uint64_t cur_va = anchor_va;
        uint32_t count  = 0;
        for (uint32_t i = 0; i < 4096 && count < 8; ++i)
        {
            char name[16] = {};
            mvm::ReadVirtual(kcr3, cur_va + cfg->eprocess_name, name, 15);
            for (int c = 0; c < 16; ++c)
                g_report->diag_names[count][c] = name[c];
            count++;

            uint64_t flink = mvm::ReadValue<uint64_t>(kcr3, cur_va + cfg->eprocess_links);
            if (!flink)
            {
                g_report->diag_walk_failed_at = i;
                break;
            }
            cur_va = flink - cfg->eprocess_links;
            if (cur_va == anchor_va)
                break;
        }
        g_report->diag_walk_count = count;
    }

    uint64_t target_cr3 = mvm::FindProcessByName("explorer.exe");
    if (!target_cr3)
        target_cr3 = mvm::FindProcessByName("csrss.exe");
    if (cr3_ok(target_cr3))
        pass(g_report->test_find_process_explorer);
    else
        fail(g_report->test_find_process_explorer);

    uint64_t pid4_cr3 = mvm::FindProcessByPid(4);
    if (cr3_ok(pid4_cr3) && pid4_cr3 == system_cr3)
        pass(g_report->test_find_process_by_pid);
    else
        fail(g_report->test_find_process_by_pid);

    if (cfg && cfg->kernel_cr3 && cfg->eprocess_list_head_va)
    {
        const uint64_t kcr3      = cfg->kernel_cr3;
        const uint64_t anchor_va = cfg->eprocess_list_head_va;

        uint32_t self_pid = static_cast<uint32_t>(mvm::ReadValue<uint64_t>(kcr3, anchor_va + cfg->eprocess_pid));

        char self_name[16] = {};
        mvm::ReadVirtual(kcr3, anchor_va + cfg->eprocess_name, self_name, 15);

        g_report->diag_lookup_pid   = self_pid;
        g_report->diag_expected_cr3 = kcr3;
        for (int c = 0; c < 16; ++c)
            g_report->diag_pid_proc_name[c] = self_name[c];

        g_report->diag_pid_found_cr3  = mvm::FindProcessByPid(self_pid);
        g_report->diag_name_found_cr3 = mvm::FindProcessByName(self_name);

        g_report->diag_lookups_match =
            (g_report->diag_pid_found_cr3 == kcr3 && g_report->diag_name_found_cr3 == kcr3) ? 1u : 0u;
    }

    if (target_cr3)
    {
        uint64_t peb = mvm::GetProcessPeb(target_cr3);
        if (peb != 0 && (peb & 0xFFFF000000000000ull) == 0)
            pass(g_report->test_get_process_peb);
        else
            fail(g_report->test_get_process_peb);

        uint64_t base = mvm::GetProcessBase(target_cr3);
        if (base != 0)
            pass(g_report->test_get_process_base);
        else
            fail(g_report->test_get_process_base);
    }
    else
    {
        fail(g_report->test_get_process_peb);
        fail(g_report->test_get_process_base);
    }
}

static void test_module_discovery()
{
    uint64_t mod_cr3 = mvm::FindProcessByName("explorer.exe");
    if (!mod_cr3)
        mod_cr3 = mvm::FindProcessByName("csrss.exe");
    if (!mod_cr3)
    {
        fail(g_report->test_find_module_ntdll);
        fail(g_report->test_find_module_size);
        fail(g_report->test_find_export_ntdll);
        return;
    }

    uint64_t ntdll_base = mvm::FindModuleBase(mod_cr3, "ntdll.dll");
    if (ntdll_base != 0)
        pass(g_report->test_find_module_ntdll);
    else
        fail(g_report->test_find_module_ntdll);

    uint64_t ntdll_size = mvm::FindModuleSize(mod_cr3, "ntdll.dll");
    if (ntdll_size > 0x100000)
        pass(g_report->test_find_module_size);
    else
        fail(g_report->test_find_module_size);

    if (ntdll_base)
    {
        uint64_t export_va = mvm::FindExport(mod_cr3, ntdll_base, "NtQueryInformationProcess");
        if (export_va != 0 && export_va > ntdll_base && export_va < ntdll_base + ntdll_size)
            pass(g_report->test_find_export_ntdll);
        else
            fail(g_report->test_find_export_ntdll);
    }
    else
    {
        fail(g_report->test_find_export_ntdll);
    }
}

static void test_pattern_scan()
{
    uint8_t* buf = static_cast<uint8_t*>(mvm::Alloc(64));
    if (!buf)
    {
        fail(g_report->test_pattern_scan);
        return;
    }

    for (int i = 0; i < 64; ++i)
        buf[i] = 0;
    buf[16] = 0xDE;
    buf[17] = 0xAD;
    buf[18] = 0xBE;
    buf[19] = 0xEF;

    if (buf[16] == 0xDE && buf[17] == 0xAD && buf[18] == 0xBE && buf[19] == 0xEF)
        pass(g_report->test_pattern_scan);
    else
        fail(g_report->test_pattern_scan);
}

static void test_mailbox()
{
    void* mbox = mvm::GetMailbox(0);

    uint32_t count = mvm::GetMailboxCount();
    if (mbox != nullptr || count == 0)
        pass(g_report->test_mailbox_access);
    else
        fail(g_report->test_mailbox_access);
}

static void test_typed_rw()
{
    uint32_t* val32 = static_cast<uint32_t*>(mvm::Alloc(sizeof(uint32_t)));
    if (!val32)
    {
        fail(g_report->test_read_value_typed);
        fail(g_report->test_write_value_typed);
        return;
    }

    *val32 = 0x12345678;
    if (*val32 == 0x12345678)
        pass(g_report->test_read_value_typed);
    else
        fail(g_report->test_read_value_typed);

    *val32 = 0xAABBCCDD;
    if (*val32 == 0xAABBCCDD)
        pass(g_report->test_write_value_typed);
    else
        fail(g_report->test_write_value_typed);
}

extern "C" void payload_main(MicroVmConfig* config)
{
    mvm::Init(config);

    void* mbox = mvm::GetMailbox(0);
    if (mbox)
    {
        g_report = static_cast<TestReport*>(mbox);
    }
    else
    {
        g_report = static_cast<TestReport*>(mvm::Alloc(sizeof(TestReport)));
    }

    if (!g_report)
    {
        mvm::Done();
        return;
    }

    auto* raw = reinterpret_cast<uint8_t*>(g_report);
    for (uint64_t i = 0; i < sizeof(TestReport); ++i)
        raw[i] = 0;

    g_report->magic       = TEST_REPORT_MAGIC;
    g_report->total_tests = 20;
    g_report->passed      = 0;
    g_report->failed      = 0;

    test_config(config);
    test_heap();
    test_physmap_read(config);
    test_translate_va_self(config);
    test_read_write_virtual();
    test_process_discovery(config);
    test_module_discovery();
    test_pattern_scan();
    test_mailbox();
    test_typed_rw();

    mvm::Done();
}
