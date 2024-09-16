#ifndef _SHAMPOOS_MM_H_
#define _SHAMPOOS_MM_H_

#define PAGE_SIZE        0x1000
#define MIDDLE_PAGE_SIZE 0x200000
#define HUGE_PAGE_SIZE   0x40000000

#define KiloBytes 0x400
#define MegaBytes 0x100000
#define GigaBytes 0x40000000

#define ROUND_UP(x, align)   ((x + (align - 1)) & ~(align - 1))
#define ROUND_DOWN(x, align) (x & ~(align - 1))

#define ALIGNED(x, align) ((x & (align - 1)) == 0)

enum ENTRY_FLAGS {
        PAGE_ENTRY_WRITE = 1 << 0,
        PAGE_ENTRY_READ = 1 << 1,
        PAGE_ENTRY_EXEC = 1 << 2,
        PAGE_ENTRY_USER = 1 << 3,
        PAGE_ENTRY_DEVICE = 1 << 4,
        PAGE_ENTRY_UNCACHED = 1 << 5,
};
/*
    here we call the common flags entry flags
    and the flags in arch as page flags(pflags)
*/
#endif