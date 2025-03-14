#ifndef _SHAMPOOS_TRAP_DEF_H_
#define NR_IRQ                                                              \
        1084 /*in gic(v2) we can only use 0-1019, and we use 64 as the sync \
                trap*/
#define AARCH64_IRQ_OFFSET 64

#define TRAP_TYPE_SYNC 1
#define TRAP_TYPE_IRQ  2
#define TRAP_TYPE_FIQ  3
#endif