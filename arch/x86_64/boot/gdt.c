#include <shampoos/percpu.h>
#include <arch/x86_64/desc.h>

DEFINE_PER_CPU(struct seg_desc, gdt[GDT_SIZE]) = {{0},
                                                  {
                                                          .type = 0xe,
                                                          .p = 1,
                                                          .s = 1,
                                                          .l = 1,
                                                  }};
DEFINE_PER_CPU(struct pseudo_descriptor, gdt_desc) = {
        .limit = GDT_SIZE * sizeof(struct seg_desc) - 1,
        .base_addr = (u64)&gdt,
};
// remember that it's percpu
// so must be fill in the gdt when smp setup with it's cpuid