#ifndef _RENDEZVOS_BITMAP_H_
#define _RENDEZVOS_BITMAP_H_

#include <common/align.h>
#include <common/bit.h>

#define CELL_TYPE u64

#define CELL_SIZE (sizeof(CELL_TYPE) * 8)

#define DEFINE_BITMAP(name, bits) \
        (CELL_TYPE name[ROUND_UP(bits, CELL_SIZE) / 8])

#endif