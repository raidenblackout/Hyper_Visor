#ifndef SNP_SEND_H
#define SNP_SEND_H

#include <stddef.h>
#include "netlog_cfg.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int SnpSendInit(void* system_table, netlog_cfg_v4* cfg, void (*log_fn)(const char*));

    void SnpSendUdpLine(const char* line, size_t len);

    void SnpSendShutdown(void);

    int SnpSendIsArmed(void);

#ifdef __cplusplus
}
#endif

#endif
