#pragma once
#include "microvm_config.h"

namespace mvm
{

void Init(MicroVmConfig* config_ptr);

void*          PhysToVirt(uint64_t pa);
volatile void* PhysToVirtIO(uint64_t pa);

uint64_t TranslateVA(uint64_t cr3, uint64_t va);

bool ReadPhysical(uint64_t pa, void* dest, uint64_t size);
bool WritePhysical(uint64_t pa, const void* src, uint64_t size);

bool ReadVirtual(uint64_t cr3, uint64_t va, void* dest, uint64_t size);
bool WriteVirtual(uint64_t cr3, uint64_t va, const void* src, uint64_t size);

template <typename T> T ReadValue(uint64_t cr3, uint64_t va)
{
    T val{};
    ReadVirtual(cr3, va, &val, sizeof(T));
    return val;
}

template <typename T> void WriteValue(uint64_t cr3, uint64_t va, T value)
{
    WriteVirtual(cr3, va, &value, sizeof(T));
}

uint64_t FindProcessByPid(uint32_t pid);
uint64_t FindProcessByName(const char* name);
uint64_t FindFirstProcessByName(const char* name);
uint64_t FindLastProcessByName(const char* name);
uint32_t FindProcessesByName(const char* name, uint64_t* out_cr3s, uint32_t max_count);

uint64_t GetProcessBase(uint64_t cr3);
uint64_t GetProcessPeb(uint64_t cr3);

uint64_t FindModuleBase(uint64_t cr3, const char* name);
uint64_t FindModuleSize(uint64_t cr3, const char* name);
uint64_t FindExport(uint64_t cr3, uint64_t module_base, const char* func_name);

uint64_t PatternScan(uint64_t cr3, uint64_t start_va, uint64_t size, const uint8_t* pattern, const char* mask);

void*    GetMailbox(uint32_t index);
uint32_t GetMailboxCount();

void*    Alloc(uint64_t size);
void     Free(void* ptr);
uint64_t GetHeapRemaining();

void Done();
void Yield();

}
