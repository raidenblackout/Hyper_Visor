#include "vmmcall_handler.h"
#include "svm/vmcb.h"
#include "mvm_shared.h"
#include "boot_stubs.h"
#include "fb_panic.h"
#include "micro_vm.h"
#include "vmexit_log.h"
#include "net/net_log.h"
#include <intrin.h>

namespace vmm
{

namespace
{

void inject_ud(HOST_CONTEXT* ctx)
{
    auto& vmcb = ctx->Cpu->GuestVmcb;

    vmcb.ControlArea.EventInj = (1ULL << 31) | (3ULL << 8) | 6;
}

void advance_past_vmmcall(HOST_CONTEXT* ctx)
{
    SvmAdvanceRip(&ctx->Cpu->GuestVmcb, 3);
}

}

void dispatch_vmmcall(HOST_CONTEXT* ctx)
{
    auto* gr = ctx->Cpu->States.Guest.Registers;

    if (gr->R10 != VMMCALL_PRESHARED_KEY)
    {
        inject_ud(ctx);
        return;
    }

    const uint64_t reason = gr->Rcx;

    switch (reason)
    {
    case VMMCALL_HYPERVISOR_PRESENT:
    {
        const uint32_t cpu = ctx->Cpu->States.Guest.ProcessorNumber;
        boot_stubs::log_mem("vmmcall: ping cpu%u", cpu);
        net_log::flush();
        gr->Rax = 1;
        break;
    }

    case VMMCALL_REQUEST_SHUTDOWN:

        gr->Rax = 0;
        break;

    case VMMCALL_MICROVM_CREATE:
    {
        micro_vm::instance_t* inst = micro_vm::create(gr->Rdx);
        gr->Rax                    = inst ? inst->handle : 0;
        break;
    }

    case VMMCALL_MICROVM_DESTROY:
    {
        micro_vm::instance_t* inst = micro_vm::find(gr->Rdx);
        gr->Rax                    = (inst && micro_vm::destroy(inst)) ? 1 : 0;
        break;
    }

    case VMMCALL_STEP_MICROVM:
    {
        micro_vm::instance_t* inst = micro_vm::find(gr->Rdx);
        if (!inst)
        {
            gr->Rax = static_cast<uint64_t>(-1LL);
            break;
        }
        gr->Rax = static_cast<uint64_t>(micro_vm::step(inst));
        break;
    }

    case VMMCALL_MICROVM_LOAD:
    {
        const uint64_t caller_cr3 = ctx->Cpu->GuestVmcb.StateSaveArea.Cr3;
        uint64_t       req_pa     = 0;
        if (!micro_vm::guest_va_to_hpa(caller_cr3, gr->Rdx, &req_pa))
        {
            gr->Rax = 0;
            break;
        }

        mvm_microvm_load_t req = {};
        __movsb(reinterpret_cast<uint8_t*>(&req), reinterpret_cast<const uint8_t*>(req_pa), sizeof(req));

        micro_vm::instance_t* inst = micro_vm::find(req.handle);
        if (!inst)
        {
            gr->Rax = 0;
            break;
        }

        gr->Rax = micro_vm::load_from_guest(
                      inst, &ctx->Cpu->GuestVmcb, req.code_va, req.code_size, req.stack_size, req.xor_key)
                      ? 1
                      : 0;
        break;
    }

    case VMMCALL_MICROVM_MAP_MAILBOX:
    {
        micro_vm::instance_t* inst = micro_vm::find(gr->Rdx);
        if (!inst)
        {
            gr->Rax = 0;
            break;
        }

        const uint64_t caller_cr3 = ctx->Cpu->GuestVmcb.StateSaveArea.Cr3;
        uint64_t       mailbox_pa = 0;
        if (!micro_vm::guest_va_to_hpa(caller_cr3, gr->R8, &mailbox_pa))
        {
            gr->Rax = 0;
            break;
        }
        gr->Rax = micro_vm::map_mailbox(inst, mailbox_pa) ? 1 : 0;
        break;
    }

    case VMMCALL_HV_DRAIN_VMEXIT_LOG:
    {
        const uint64_t caller_cr3  = ctx->Cpu->GuestVmcb.StateSaveArea.Cr3;
        const uint64_t user_buf_va = gr->Rdx;
        const uint32_t max_entries = static_cast<uint32_t>(gr->R8);
        const uint64_t handle_va   = gr->R9;
        if (max_entries == 0 || user_buf_va == 0 || handle_va == 0)
        {
            gr->Rax = 0;
            break;
        }

        if (((handle_va & 0xFFFULL) + sizeof(uint64_t) * 2) > 0x1000ULL)
        {
            gr->Rax = 0;
            break;
        }
        uint64_t handle_pa = 0;
        if (!micro_vm::guest_va_to_hpa(caller_cr3, handle_va, &handle_pa))
        {
            gr->Rax = 0;
            break;
        }
        auto* handle = reinterpret_cast<uint64_t*>(handle_pa);

        constexpr uint32_t per_call_cap = 4096 / sizeof(vmexit_log::entry_t);
        const uint32_t     cap          = max_entries < per_call_cap ? max_entries : per_call_cap;

        uint64_t cursor        = handle[0];
        uint64_t dropped_total = 0;
        uint32_t total_written = 0;
        uint64_t cur_va        = user_buf_va;
        uint32_t remaining     = cap;

        while (remaining > 0)
        {
            const uint64_t page_va       = cur_va & ~0xFFFULL;
            const uint64_t offset_in_pg  = cur_va & 0xFFFULL;
            const uint64_t bytes_left_pg = 0x1000ULL - offset_in_pg;
            uint32_t       fits_this_pg  = static_cast<uint32_t>(bytes_left_pg / sizeof(vmexit_log::entry_t));
            if (fits_this_pg == 0)
                break;
            if (fits_this_pg > remaining)
                fits_this_pg = remaining;

            uint64_t page_pa = 0;
            if (!micro_vm::guest_va_to_hpa(caller_cr3, page_va, &page_pa))
                break;

            uint64_t       dropped = 0;
            const uint32_t n       = vmexit_log::drain(
                reinterpret_cast<vmexit_log::entry_t*>(page_pa + offset_in_pg), fits_this_pg, &cursor, &dropped);
            dropped_total += dropped;
            total_written += n;
            if (n < fits_this_pg)
                break;
            cur_va    += static_cast<uint64_t>(n) * sizeof(vmexit_log::entry_t);
            remaining -= n;
        }

        handle[0] = cursor;
        handle[1] = dropped_total;
        gr->Rax   = total_written;
        break;
    }

    case VMMCALL_NETLOG_CONTROL:
    {
        const uint64_t cmd = gr->Rdx;
        if (cmd == 0)
            net_log::set_enabled(false);
        else if (cmd == 1)
            net_log::set_enabled(true);
        gr->Rax = net_log::is_enabled() ? 1 : 0;
        break;
    }

    case VMMCALL_FEATURES_CONTROL:
    {
        const uint64_t     id  = gr->Rdx;
        const uint64_t     cmd = gr->R8;
        constexpr uint64_t ERR = (uint64_t)1 << 63;

        auto apply = [](uint64_t c, auto setter, auto getter) -> uint64_t
        {
            if (c == 0)
                setter(false);
            else if (c == 1)
                setter(true);
            return getter() ? 1u : 0u;
        };

        switch (id)
        {
        case HV_FEATURE_ID_FB_LOG:
            gr->Rax = apply(cmd, [](bool v) { fb_panic::set_enabled(v); }, []() { return fb_panic::is_enabled(); });
            break;
        case HV_FEATURE_ID_LOG_MEM:
            gr->Rax = apply(
                cmd,
                [](bool v) { boot_stubs::set_log_mem_enabled(v); },
                []() { return boot_stubs::log_mem_enabled(); });
            break;
        case HV_FEATURE_ID_NETLOG:
            gr->Rax = apply(cmd, [](bool v) { net_log::set_enabled(v); }, []() { return net_log::is_enabled(); });
            break;
        default:
            gr->Rax = ERR;
            break;
        }
        break;
    }

    case VMMCALL_MICROVM_GET_STATS:
    {
        micro_vm::instance_t* inst = micro_vm::find(gr->Rdx);
        if (!inst)
        {
            gr->Rax = 0;
            break;
        }
        const uint64_t caller_cr3 = ctx->Cpu->GuestVmcb.StateSaveArea.Cr3;
        uint64_t       buf_pa     = 0;
        if (!micro_vm::guest_va_to_hpa(caller_cr3, gr->R8, &buf_pa))
        {
            gr->Rax = 0;
            break;
        }
        auto* buf = reinterpret_cast<uint64_t*>(buf_pa);
        buf[0]    = inst->n_steps;
        buf[1]    = inst->n_exit_intr;
        buf[2]    = inst->n_exit_vintr;
        buf[3]    = inst->n_exit_vmmcall_done;
        buf[4]    = inst->n_exit_vmmcall_yield;
        buf[5]    = inst->n_exit_other;
        gr->Rax   = 1;
        break;
    }

    default:

        gr->Rax = 2;
        break;
    }

    advance_past_vmmcall(ctx);
}

}
