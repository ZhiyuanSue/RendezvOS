#ifndef _RENDEZVOS_MULTIBOOT_
#define _RENDEZVOS_MULTIBOOT_
#include <common/types.h>
#define MULTIBOOT_MAGIC 0x2BADB002

struct multiboot_mem {
        u32 mem_lower;
        u32 mem_upper;
} __attribute__((packed));

struct multiboot_mods {
        u32 mods_count;
        u32 mods_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
        u32 size;
        u64 addr;
        u64 len;
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5
        u32 type;
} __attribute__((packed));

struct multiboot_mmap {
        u32 mmap_length;
        u32 mmap_addr;
} __attribute__((packed));
#define for_each_multiboot_mmap(addr_ptr, length)                            \
        for (struct multiboot_mmap_entry *mmap =                             \
                     (struct multiboot_mmap_entry *)addr_ptr;                \
             ((vaddr)mmap) < (addr_ptr + length);                            \
             mmap = (struct multiboot_mmap_entry *)((vaddr)mmap + mmap->size \
                                                    + sizeof(mmap->size)))

struct multiboot_drivers {
        u32 drives_length;
        u32 drives_addr;
} __attribute__((packed));

struct multiboot_vbe {
        u32 vbe_control_info;
        u32 vbe_mode_info;
        u16 vbe_mode;
        u16 vbe_interface_seg;
        u16 vbe_interface_off;
        u16 vbe_interface_len;
} __attribute__((packed));

struct multiboot_color {
        u8 red;
        u8 green;
        u8 blue;
};

struct multiboot_framebuffer {
        u64 framebuffer_addr;
        u32 framebuffer_pitch;
        u32 framebuffer_width;
        u32 framebuffer_height;
        u8 framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED  0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB      1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2
        u8 framebuffer_type;
        union {
                struct {
                        u32 framebuffer_palette_addr;
                        u16 framebuffer_palette_num_colors;
                };
                struct {
                        u8 framebuffer_red_field_position;
                        u8 framebuffer_red_mask_size;
                        u8 framebuffer_green_field_position;
                        u8 framebuffer_green_mask_size;
                        u8 framebuffer_blue_field_position;
                        u8 framebuffer_blue_mask_size;
                };
        };
} __attribute__((packed));

struct multiboot_info {
#define MULTIBOOT_INFO_FLAG_MEM                (1 << 0)
#define MULTIBOOT_INFO_FLAG_BOOT_DEVICE        (1 << 1)
#define MULTIBOOT_INFO_FLAG_CMD                (1 << 2)
#define MULTIBOOT_INFO_FLAG_MOD                (1 << 3)
#define MULTIBOOT_INFO_FLAG_BYTE_28_TYPE1      (1 << 4)
#define MULTIBOOT_INFO_FLAG_BYTE_28_TYPE2      (1 << 5)
#define MULTIBOOT_INFO_FLAG_MMAP               (1 << 6)
#define MULTIBOOT_INFO_FLAG_DRIVER             (1 << 7)
#define MULTIBOOT_INFO_FLAG_CFG_TABLE          (1 << 8)
#define MULTIBOOT_INFO_FLAG_BOOTLOADER_NAME    (1 << 9)
#define MULTIBOOT_INFO_FLAG_APM_TBALE          (1 << 10)
#define MULTIBOOT_INFO_FLAG_VBE                (1 << 11)
#define MULTIBOOT_INFO_FLAG_FRAMEBUFFER        (1 << 12)
#define MULTIBOOT_INFO_FLAG_CHECK(flags, flag) ((flags & flag))
        u32 flags;
        struct multiboot_mem mem;
        u32 boot_device;
        u32 cmdline;
        struct multiboot_mods mods;
        u32 syms[4]; /*we use bin file and a.out or elf info is unused*/
        struct multiboot_mmap mmap;
        struct multiboot_drivers drivers;
        u32 config_table;
        u32 boot_loader_name;
        u32 apm_table;
        struct multiboot_vbe vbe;
        struct multiboot_framebuffer framebuffer;
} __attribute__((packed));

#endif
