#ifndef _SHAMPOOS_X86_DESC_H_
#define _SHAMPOOS_X86_DESC_H_
#include <common/types.h>

#define GDT_SIZE            5
#define GDT_TSS_LOWER_INDEX 2
#define GDT_TSS_UPPER_INDEX 3

struct pseudo_descriptor {
        u16 limit;
        u64 base_addr;
} __attribute__((packed));

union desc_selector {
        u16 selector;
        struct {
                u16 rpl : 2;
                u16 table_indicator : 1;
                u16 index : 13;
        };
} __attribute__((packed));

union desc {
        u64 desc_value;
        /* segment descriptors */
        struct {
                u32 limit_15_0 : 16; // low bits of segment limit
                u32 base_address_15_0 : 16; // low bits of segment base address
                u32 base_address_23_16 : 8; // middle bits of segment base
                                            // address
                u32 type : 4; // segment type (see STS_ constants)
                u32 s : 1; // 0 = system, 1 = application
                u32 dpl : 2; // descriptor Privilege Level
                u32 p : 1; // present
                u32 limit_19_16 : 4; // high bits of segment limit
                u32 avl : 1; // unused (available for software use)
                u32 l : 1; // 64-bit code segment
                u32 d_or_b : 1; // 0 = 16-bit segment, 1 = 32-bit segment
                u32 g : 1; // granularity: limit scaled by 4K when set
                u32 base_address_31_24 : 8; // high bits of segment base address
        } seg_desc;
        /*tss (and ldt) descriptor lower part*/
        struct {
                /*first u32*/
                u16 limit_15_0;
                u16 base_address_15_0;
                /*second u32*/
                u32 base_address_23_16 : 8;
                u32 type : 4;
                u32 zero_0 : 1;
                u32 dpl : 2;
                u32 p : 1;
                u32 limit_19_16 : 4;
                u32 avl : 1;
                u32 zero_1 : 1;
                u32 zero_2 : 1;
                u32 g : 1;
                u32 base_address_31_24 : 8;
        } tss_ldt_desc_lower;
        /*tss (and ldt) descriptor upper part*/
        struct {
                /*third u32*/
                u32 base_address_63_32;
                /*forth u32*/
                u32 res_0 : 8;
                u32 zero_3 : 5;
                u32 res_1 : 19;
        } tss_ldt_desc_upper;
} __attribute__((packed));
/*idt gate descriptors*/
union idt_gate_desc {
        u64 idt[2];
        struct {
                /*first u32*/
                u16 offset_15_0;
                union desc_selector seg_selector;
                /*second u32*/
                u32 ist : 3;
                u32 zero_0 : 5;
                u32 type : 4;
                u32 zero_1 : 1;
                u32 dpl : 2;
                u32 p : 1;
                u32 offset_31_16 : 16;
                /*third u32*/
                u32 offset_63_32;
                /*forth u32*/
                u32 res;
        };
} __attribute__((packed));
#define IA32E_CALL_GATE_TYPE     0xC
#define IA32E_IDT_GATE_TYPE_INT  0xE
#define IA32E_IDT_GATE_TYPE_TRAP 0xF
#define SET_IDT_GATE(gate, offset, sel, _dpl, _type, _ist)            \
        {                                                             \
                gate.res = 0;                                         \
                gate.offset_63_32 = (u32)((offset) >> 32);            \
                gate.offset_31_16 = (u32)(offset & 0xffffffff) >> 16; \
                gate.p = 1;                                           \
                gate.dpl = _dpl;                                      \
                gate.zero_1 = 0;                                      \
                gate.type = _type;                                    \
                gate.zero_0 = 0;                                      \
                gate.ist = _ist;                                      \
                gate.seg_selector = sel;                              \
                gate.offset_15_0 = (u32)(offset & 0xffff);            \
        }

#define IA32E_LDT_TYPE        0x2
#define IA32E_TSS_TYPE_VALID  0x9
#define IA32E_TSS_TYPE_IN_USE 0xB
#define SET_TSS_LDT_DESC_LOWER(_desc, base_address, _dpl)                   \
        {                                                                   \
                _desc.zero_0 = 0;                                           \
                _desc.zero_1 = 0;                                           \
                _desc.zero_2 = 0;                                           \
                _desc.dpl = _dpl;                                           \
                _desc.type = IA32E_TSS_TYPE_VALID;                          \
                _desc.limit_15_0 = (u32)(0x67 & 0xffff);                    \
                _desc.limit_19_16 = (u32)(0x67 & 0xf0000) >> 16;            \
                _desc.p = 1;                                                \
                _desc.avl = 1;                                              \
                _desc.g = 0;                                                \
                _desc.base_address_15_0 = (u32)(base_address & 0xffff);     \
                _desc.base_address_23_16 = (u32)(base_address & 0xff0000)   \
                                           >> 16;                           \
                _desc.base_address_31_24 = (u32)(base_address & 0xff000000) \
                                           >> 24;                           \
        }
#define SET_TSS_LDT_DESC_UPPER(_desc, base_address)                     \
        {                                                               \
                _desc.res_0 = 0;                                        \
                _desc.res_1 = 0;                                        \
                _desc.zero_3 = 0;                                       \
                _desc.base_address_63_32 = (u32)((base_address) >> 32); \
        }
#endif