#include <shampoos/percpu.h>
#include <arch/x86_64/desc.h>

DEFINE_PER_CPU(union desc, gdt[GDT_SIZE]) = {{.seg_desc = {0}},
                                             {.seg_desc =
                                                      {
                                                              .type = 0xe,
                                                              .p = 1,
                                                              .s = 1,
                                                              .l = 1,
                                                      }}, /*KERNEL segment
                                                             desc*/
                                             {.tss_ldt_desc_lower = {0}}, /* tss
                                                                           */
                                             {.tss_ldt_desc_upper = {0}}};

DEFINE_PER_CPU(struct pseudo_descriptor, gdt_desc) = {
        .limit = GDT_SIZE * sizeof(union desc) - 1,
        .base_addr = (u64)&gdt,
};
// remember that it's percpu
// so must be fill in the gdt when smp setup with it's cpuid