#ifndef _SHAMPOOS_SLUB_H_
#define _SHAMPOOS_SLUB_H_

#include "pmm.h"
#include "vmm.h"
#include <common/types.h>
#define MAX_SLUB_SLOTS 10

struct slub {
        MM_COMMON;
        struct pmm *pmm;
};

#endif