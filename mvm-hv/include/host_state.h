#pragma once

#include "common.h"

void InitializeHostSharedData(SHARED_HOST_DATA* shd, uint32_t procCount, uint16_t hostCs);

void InitializeHostData(HOST_DATA* hd, const SHARED_HOST_DATA* shd);

void SwitchToHostContext(const SHARED_HOST_DATA* shd, const HOST_DATA* hd);
