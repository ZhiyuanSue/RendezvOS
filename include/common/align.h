#ifndef _RENDEZVOS_ALIGN_H_
#define _RENDEZVOS_ALIGN_H_

#define ROUND_UP(x, align)   ((x + (align - 1)) & ~(align - 1))
#define ROUND_DOWN(x, align) (x & ~(align - 1))

#define ALIGNED(x, align) ((x & (align - 1)) == 0)
#endif