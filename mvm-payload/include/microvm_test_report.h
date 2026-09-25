#pragma once
#include <cstdint>

enum TestResult : uint64_t
{
    TEST_NOT_RUN = 0,
    TEST_PASS    = 1,
    TEST_FAIL    = 2,
};

struct TestReport
{
    uint64_t magic;
    uint64_t total_tests;
    uint64_t passed;
    uint64_t failed;

    uint64_t test_config_magic;
    uint64_t test_physmap_base;
    uint64_t test_heap_alloc;
    uint64_t test_heap_remaining;
    uint64_t test_heap_overflow;
    uint64_t test_physmap_read;
    uint64_t test_translate_va_self;
    uint64_t test_read_write_virtual;
    uint64_t test_find_process_system;
    uint64_t test_find_process_explorer;
    uint64_t test_find_process_by_pid;
    uint64_t test_get_process_base;
    uint64_t test_get_process_peb;
    uint64_t test_find_module_ntdll;
    uint64_t test_find_module_size;
    uint64_t test_find_export_ntdll;
    uint64_t test_pattern_scan;
    uint64_t test_mailbox_access;
    uint64_t test_read_value_typed;
    uint64_t test_write_value_typed;

    uint32_t diag_walk_count;
    uint32_t diag_walk_failed_at;
    char     diag_names[8][16];

    uint32_t diag_lookup_pid;
    uint32_t diag_lookups_match;
    char     diag_pid_proc_name[16];
    uint64_t diag_pid_found_cr3;
    uint64_t diag_name_found_cr3;
    uint64_t diag_expected_cr3;
};

constexpr uint64_t TEST_REPORT_MAGIC = 0x54534554;

struct TestEntry
{
    const char* name;
    uint64_t TestReport::* field;
};

static const TestEntry g_test_entries[] = {
    { "config_magic", &TestReport::test_config_magic },
    { "physmap_base", &TestReport::test_physmap_base },
    { "heap_alloc", &TestReport::test_heap_alloc },
    { "heap_remaining", &TestReport::test_heap_remaining },
    { "heap_overflow", &TestReport::test_heap_overflow },
    { "physmap_read", &TestReport::test_physmap_read },
    { "translate_va_self", &TestReport::test_translate_va_self },
    { "read_write_virtual", &TestReport::test_read_write_virtual },
    { "find_process_system", &TestReport::test_find_process_system },
    { "find_process_explorer", &TestReport::test_find_process_explorer },
    { "find_process_by_pid", &TestReport::test_find_process_by_pid },
    { "get_process_base", &TestReport::test_get_process_base },
    { "get_process_peb", &TestReport::test_get_process_peb },
    { "find_module_ntdll", &TestReport::test_find_module_ntdll },
    { "find_module_size", &TestReport::test_find_module_size },
    { "find_export_ntdll", &TestReport::test_find_export_ntdll },
    { "pattern_scan", &TestReport::test_pattern_scan },
    { "mailbox_access", &TestReport::test_mailbox_access },
    { "read_value_typed", &TestReport::test_read_value_typed },
    { "write_value_typed", &TestReport::test_write_value_typed },
};
