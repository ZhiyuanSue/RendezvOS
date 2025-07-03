#ifndef _RENDEZVOS_MULTIBOOT2_
#define _RENDEZVOS_MULTIBOOT2_
#include <common/types.h>
#define MULTIBOOT2_MAGIC 0x36D76289
/*this magic is used in eax*/

struct multiboot2_info {
        u32 total_size;
        u32 reserved;
} __attribute__((packed));

#define MULTIBOOT2_TAG_COMMON \
        u32 type;             \
        u32 size
#define MULTIBOOT2_TAG_ALIGN                 8
#define MULTIBOOT2_TAG_TYPE_END              0
#define MULTIBOOT2_TAG_TYPE_CMDLINE          1
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT2_TAG_TYPE_MODULE           3
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO    4
#define MULTIBOOT2_TAG_TYPE_BOOTDEV          5
#define MULTIBOOT2_TAG_TYPE_MMAP             6
#define MULTIBOOT2_TAG_TYPE_VBE              7
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER      8
#define MULTIBOOT2_TAG_TYPE_ELF_SECTIONS     9
#define MULTIBOOT2_TAG_TYPE_APM              10
#define MULTIBOOT2_TAG_TYPE_EFI32            11
#define MULTIBOOT2_TAG_TYPE_EFI64            12
#define MULTIBOOT2_TAG_TYPE_SMBIOS           13
#define MULTIBOOT2_TAG_TYPE_ACPI_OLD         14
#define MULTIBOOT2_TAG_TYPE_ACPI_NEW         15
#define MULTIBOOT2_TAG_TYPE_NETWORK          16
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP         17
#define MULTIBOOT2_TAG_TYPE_EFI_BS           18
#define MULTIBOOT2_TAG_TYPE_EFI32_IH         19
#define MULTIBOOT2_TAG_TYPE_EFI64_IH         20
#define MULTIBOOT2_TAG_TYPE_LOAD_BASE_ADDR   21
struct multiboot2_tag {
        MULTIBOOT2_TAG_COMMON;
} __attribute__((packed));
#define for_each_tag(mtb2_info)                                         \
        for (struct multiboot2_tag *tag =                               \
                     (struct multiboot2_tag *)(((vaddr)mtb2_info) + 8); \
             tag->type != MULTIBOOT2_TAG_TYPE_END;                      \
             tag = (struct multiboot2_tag *)(((vaddr)tag)               \
                                             + ROUND_UP(tag->size,      \
                                                        MULTIBOOT2_TAG_ALIGN)))

struct multiboot2_tag_string {
        MULTIBOOT2_TAG_COMMON;
        char string[0];
} __attribute__((packed));

struct multiboot2_tag_module {
        MULTIBOOT2_TAG_COMMON;
        u32 mod_start;
        u32 mod_end;
        char cmdline[0];
} __attribute__((packed));

struct multiboot2_tag_basic_meminfo {
        MULTIBOOT2_TAG_COMMON;
        u32 mem_lower;
        u32 mem_upper;
} __attribute__((packed));

struct multiboot2_tag_bootdev {
        MULTIBOOT2_TAG_COMMON;
        u32 biosdev;
        u32 slice;
        u32 part;
} __attribute__((packed));

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
} __attribute__((packed));
struct multiboot2_tag_mmap {
        MULTIBOOT2_TAG_COMMON;
        u32 entry_size;
        u32 entry_version;
        struct multiboot2_mmap_entry entries[0];
} __attribute__((packed));

struct multiboot_tag_framebuffer_common
{
  u32 type;
  u32 size;

  u64 framebuffer_addr;
  u32 framebuffer_pitch;
  u32 framebuffer_width;
  u32 framebuffer_height;
  u8 framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT     2
  u8 framebuffer_type;
  u16 reserved;
};

struct multiboot_tag_framebuffer
{
  struct multiboot_tag_framebuffer_common common;

  union
  {
    struct
    {
      u16 framebuffer_palette_num_colors;
      struct multiboot_color framebuffer_palette[0];
    };
    struct
    {
      u8 framebuffer_red_field_position;
      u8 framebuffer_red_mask_size;
      u8 framebuffer_green_field_position;
      u8 framebuffer_green_mask_size;
      u8 framebuffer_blue_field_position;
      u8 framebuffer_blue_mask_size;
    };
  };
};

#define for_each_multiboot2_mmap(tag, addr_ptr)                       \
        for (struct multiboot2_mmap_entry *mmap =                     \
                     (struct multiboot2_mmap_entry *)addr_ptr;        \
             (vaddr)mmap < (vaddr)tag + tag->size;                    \
             mmap = (struct multiboot2_mmap_entry                     \
                             *)((vaddr)mmap                           \
                                + ((struct multiboot2_tag_mmap *)tag) \
                                          ->entry_size))
#endif