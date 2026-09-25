#pragma once

#include "common.h"

void SetupIdentityMapping(PAGING_STRUCTURES* ps, bool nestedPageTables);

#if HV_FEATURE_HVB_NPT_PROTECT

void ProtectRootContextInNpt(PAGING_STRUCTURES* ps, uint64_t start, uint64_t end);
#endif

#if HV_FEATURE_HOST_WATCH

void ProtectHostStateAreas(ROOT_CONTEXT* root, uint32_t procCount);
#endif
