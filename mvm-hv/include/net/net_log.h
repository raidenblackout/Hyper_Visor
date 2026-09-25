
#pragma once
#include <cstdint>
#include <cstddef>
#include "../../bootloader/net/netlog_cfg.h"

namespace net_log
{

struct stats
{
    uint64_t enq_ok;
    uint64_t enq_drop;
    uint64_t tx_ok;
    uint64_t tx_drop;
    uint32_t link_up;
    uint32_t flush_stall;
};

bool init(const netlog_cfg_v4& cfg, void* efi_bs = nullptr);

void enqueue(const char* line, size_t len);

void flush();

stats get_stats();
bool  is_ready();

void set_enabled(bool on);
bool is_enabled();

}