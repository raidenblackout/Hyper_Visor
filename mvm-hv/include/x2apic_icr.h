#pragma once

struct HOST_CONTEXT;

extern "C" void x2apic_emulate_icr_msr(HOST_CONTEXT* ctx);

extern "C" long g_x2apic_wrmsr_budget;
