#ifndef _X86_TSS_H_
#define _X86_TSS_H_
#include <common/types.h>
#include <rendezvos/mm/nexus.h>
/*
        here we only use ia-32e mode tss
*/
struct TSS {
        u32 res0;
        u32 rsp0_lower_32_bits;
        u32 rsp0_upper_32_bits;
        u32 rsp1_lower_32_bits;
        u32 rsp1_upper_32_bits;
        u32 rsp2_lower_32_bits;
        u32 rsp2_upper_32_bits;
        u32 res1;
        u32 res2;
        u32 ist1_lower_32_bits;
        u32 ist1_upper_32_bits;
        u32 ist2_lower_32_bits;
        u32 ist2_upper_32_bits;
        u32 ist3_lower_32_bits;
        u32 ist3_upper_32_bits;
        u32 ist4_lower_32_bits;
        u32 ist4_upper_32_bits;
        u32 ist5_lower_32_bits;
        u32 ist5_upper_32_bits;
        u32 ist6_lower_32_bits;
        u32 ist6_upper_32_bits;
        u32 ist7_lower_32_bits;
        u32 ist7_upper_32_bits;
        u32 res3;
        u32 res4;
        u16 res5;
        u16 io_map_base_addr;
} __attribute__((packed));

#define set_ist(tss_ptr, ist_n, stack_ptr)                                     \
        {                                                                      \
                (tss_ptr)->ist##ist_n##_lower_32_bits = (u32)((u64)stack_ptr); \
                (tss_ptr)->ist##ist_n##_upper_32_bits =                        \
                        (u32)(((u64)stack_ptr) >> 32);                         \
        }
#define get_ist(tss_ptr, ist_n)                         \
        ((((u64)((tss_ptr)->ist##ist_n##_upper_32_bits)) \
         << 32) + (tss_ptr)->ist##ist_n##_lower_32_bits)
#define set_rsp(tss_ptr, rsp_n, stack_ptr)                                  \
        {                                                                   \
                (tss_ptr)->rsp##rsp_n##_lower_32_bits = (u32)((u64)stack_ptr); \
                (tss_ptr)->rsp##rsp_n##_upper_32_bits =                     \
                        (u32)(((u64)stack_ptr) >> 32);                         \
        }
#define get_rsp(tss_ptr, rsp_n)                         \
        ((((u64)((tss_ptr)->rsp##rsp_n##_upper_32_bits)) \
         << 32) + (tss_ptr)->rsp##rsp_n##_lower_32_bits)
/*the stack of interrupt*/
#define RSP0_INT_NUM 0
#define IST1_INT_NUM 1

void prepare_per_cpu_tss(struct nexus_node* nexus_root);
void prepare_per_cpu_tss_desc(union desc* desc_lower, union desc* desc_upper,
                              int cpu_id);
#endif