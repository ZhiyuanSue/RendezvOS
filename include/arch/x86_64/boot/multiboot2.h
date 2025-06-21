#ifndef _RENDEZVOS_MULTIBOOT2_
#define _RENDEZVOS_MULTIBOOT2_
#include <common/types.h>
#define MULTIBOOT2_MAGIC 0x36D76289
/*this magic is used in eax*/

#define MULTIBOOT2_TAG_COMMON \
        {                     \
                u32 type;     \
                u32 size;     \
        }
struct multiboot2_tag_string {
        u32 type;
        u32 size;
        char string[0];
};

struct multiboot2_tag_module {
        u32 type;
        u32 size;
        u32 mod_start;
        u32 mod_end;
        char cmdline[0];
};

struct multiboot2_tag_basic_meminfo {
        u32 type;
        u32 size;
        u32 mem_lower;
        u32 mem_upper;
};

struct multiboot2_tag_bootdev {
        u32 type;
        u32 size;
        u32 biosdev;
        u32 slice;
        u32 part;
};

struct multiboot2_mmap_entry {
        u64 addr;
        u64 len;
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5
        u32 type;
        u32 zero;
};
struct multiboot2_tag_mmap {
        u32 type;
        u32 size;
        u32 entry_size;
        u32 entry_version;
        struct multiboot2_mmap_entry entries[0];
};
#endif