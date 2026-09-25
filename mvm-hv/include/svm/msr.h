
#pragma once
#include <cstdint>

#define MSR_IA32_EFER 0xC0000080
#define EFER_SVME     (1ULL << 12)

#define MSR_IA32_PAT 0x00000277

#define SVM_MSR_VM_CR  0xC0010114
#define VM_CR_DPD      (1ULL << 0)
#define VM_CR_R_INIT   (1ULL << 1)
#define VM_CR_DIS_A20M (1ULL << 2)
#define VM_CR_LOCK     (1ULL << 3)
#define VM_CR_SVMDIS   (1ULL << 4)

#define SVM_MSR_VM_HSAVE_PA 0xC0010117

#define SVM_MSR_PERMISSIONS_MAP_SIZE 0x2000

#define X2APIC_MSR_ICR 0x830

#define X2APIC_MSR_LVT_TIMER  0x832
#define X2APIC_MSR_INIT_COUNT 0x838
#define X2APIC_MSR_DIV_CONFIG 0x83E
#define MSR_IA32_TSC_DEADLINE 0x6E0

#define X2APIC_MSR_EOI 0x80B
