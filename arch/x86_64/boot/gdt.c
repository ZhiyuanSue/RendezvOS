#include <arch/x86_64/desc.h>
#include <rendezvos/smp/percpu.h>

DEFINE_PER_CPU(union desc, gdt[GDT_SIZE]) = {
        {.seg_desc = {0}},
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
        {.tss_ldt_desc_upper = {0}},
        {.seg_desc = {0}},
        {.seg_desc = {0}},
};

DEFINE_PER_CPU(struct pseudo_descriptor, gdt_desc) = {
        .limit = GDT_SIZE * sizeof(union desc) - 1,
        .base_addr = (u64)&gdt,
};
// remember that it's percpu
// so must be fill in the gdt when smp setup with it's cpuid
void prepare_per_cpu_new_gdt(struct pseudo_descriptor *desc, union desc *gdt)
{
        /*fill in the gdt table*/
        gdt[GDT_KERNEL_CS_INDEX].seg_desc.type = 0xa;
        gdt[GDT_KERNEL_CS_INDEX].seg_desc.p = 1;
        gdt[GDT_KERNEL_CS_INDEX].seg_desc.s = 1;
        gdt[GDT_KERNEL_CS_INDEX].seg_desc.l = 1;

        gdt[GDT_KERNEL_DS_INDEX].seg_desc.type = 0x2;
        gdt[GDT_KERNEL_DS_INDEX].seg_desc.p = 1;
        gdt[GDT_KERNEL_DS_INDEX].seg_desc.s = 1;
        gdt[GDT_KERNEL_DS_INDEX].seg_desc.l = 1;

        /*fill in the gdt desc*/
        desc->base_addr = (u64)gdt;
        desc->limit = GDT_SIZE * sizeof(union desc) - 1;
}
void perpare_per_cpu_user_gdt(union desc *gdt)
{
        /*fill in the gdt table*/
        /*user cs*/
        gdt[GDT_USER_CS_INDEX].seg_desc.type = 0xa;
        gdt[GDT_USER_CS_INDEX].seg_desc.l = 1;
        gdt[GDT_USER_CS_INDEX].seg_desc.p = 1;
        gdt[GDT_USER_CS_INDEX].seg_desc.s = 1;
        gdt[GDT_USER_CS_INDEX].seg_desc.dpl = 3;
        /*user ds*/
        gdt[GDT_USER_DS_INDEX].seg_desc.type = 0x2;
        gdt[GDT_USER_DS_INDEX].seg_desc.l = 1;
        gdt[GDT_USER_DS_INDEX].seg_desc.p = 1;
        gdt[GDT_USER_DS_INDEX].seg_desc.s = 1;
        gdt[GDT_USER_DS_INDEX].seg_desc.dpl = 3;
}