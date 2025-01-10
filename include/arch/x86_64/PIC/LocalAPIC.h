#ifndef _SHAMPOOS_LOCAL_APIC_H_
#define _SHAMPOOS_LOCAL_APIC_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <arch/x86_64/sys_ctrl.h>
#include <shampoos/time.h>
#define APIC_REG_SIZE 32
#define APIC_REG_ID   (0x2)
#define APIC_ID_MASK  (0xFF000000)
/*
        for apic id register, if xAPIC, only bit 31-24 is uesd, and all bit used
   for x2APIC
*/

#define APIC_REG_VERSION        (0x3)
#define APIC_VERSION_MASK       (0xFF)
#define APIC_MAX_LVT_ENTRY_MASK (0xFF << 16)
#define APCI_EOI_BC_SUPPORT     (0x1 << 24)

#define APIC_REG_TPR (0x8)
#define APIC_REG_PPR (0xA)
#define APIC_REG_EOI (0xB)
#define APIC_REG_LDR (0xD)
#define APIC_REG_DFR (0xE)
#define APIC_REG_SVR (0xF)

// spurious vector register
#define APIC_SVR_EOI_BC      (1 << 12)
#define APIC_SVR_FOCUS_CHECK (1 << 9)
#define APIC_SVR_SW_ENABLE   (1 << 8)
#define APIC_SVR_VEC_MASK    (0xFF)

#define APIC_REG_ISR_0 (0x10)
#define APIC_REG_ISR_1 (0x11)
#define APIC_REG_ISR_2 (0x12)
#define APIC_REG_ISR_3 (0x13)
#define APIC_REG_ISR_4 (0x14)
#define APIC_REG_ISR_5 (0x15)
#define APIC_REG_ISR_6 (0x16)
#define APIC_REG_ISR_7 (0x17)

#define APIC_REG_TMR_0 (0x18)
#define APIC_REG_TMR_1 (0x19)
#define APIC_REG_TMR_2 (0x1A)
#define APIC_REG_TMR_3 (0x1B)
#define APIC_REG_TMR_4 (0x1C)
#define APIC_REG_TMR_5 (0x1D)
#define APIC_REG_TMR_6 (0x1E)
#define APIC_REG_TMR_7 (0x1F)

#define APIC_REG_IRR_0 (0x20)
#define APIC_REG_IRR_1 (0x21)
#define APIC_REG_IRR_2 (0x22)
#define APIC_REG_IRR_3 (0x23)
#define APIC_REG_IRR_4 (0x24)
#define APIC_REG_IRR_5 (0x25)
#define APIC_REG_IRR_6 (0x26)
#define APIC_REG_IRR_7 (0x27)

#define APIC_REG_ESR              (0x28)
#define APIC_ESR_SEND_CHECK_ERR   (0x1 << 0)
#define APIC_ESR_RECV_CHECK_ERR   (0x1 << 1)
#define APIC_ESR_SEND_ACCEPT_ERR  (0x1 << 2)
#define APIC_ESR_RECV_ACCEPT_ERR  (0x1 << 3)
#define APIC_ESR_REDIRECTABLE_IPI (0x1 << 4)
#define APIC_ESR_SEND_ILLEGAL_VEC (0x1 << 5)
#define APIC_ESR_RECV_ILLEGAL_VEC (0x1 << 6)
#define APIC_ESR_ILLEGAL_REG_ADDR (0x1 << 7)

#define APIC_REG_LVT_CMCI (0x2F)
#define APIC_REG_ICR      (0x30)
#define APIC_REG_ICR_HIGH \
        (0x31) /*only used in xAPIC, no 831H MSR reg in x2APIC*/

#define APIC_REG_LVT_TIME   (0x32)
#define APIC_REG_LVT_THER   (0x33)
#define APIC_REG_LVT_PERF   (0x34)
#define APIC_REG_LVT_LINT_0 (0x35)
#define APIC_REG_LVT_LINT_1 (0x36)
#define APIC_REG_LVT_ERR    (0x37)

// Some LVT Value
#define APIC_LVT_TIMER_MODE_ONE_SHOT (0x0 << 17)
#define APIC_LVT_TIMER_MODE_PERIODIC (0x1 << 17)
#define APIC_LVT_TIMER_MODE_TSC_DDL  (0x2 << 17)
#define APIC_LVT_TIMER_MODE_MASK     (0x2 << 17)
#define APIC_LVT_MASKED              (0x00010000)
#define APIC_LVT_TRIGGER_MODE_LEVEL  (0x1 << 15)
#define APIC_LVT_REMOTE_IRR          (0x1 << 14)
#define APIC_LVT_INT_PIN_POLARITY    (0x1 << 13)
#define APIC_LVT_DEL_STATUS          (0x1 << 12)
#define APIC_LVT_DEL_MODE_FIXED      (0x0 << 8)
#define APIC_LVT_DEL_MODE_SMI        (0x2 << 8)
#define APIC_LVT_DEL_MODE_NMI        (0x4 << 8)
#define APIC_LVT_DEL_MODE_ExtINT     (0x7 << 8)
#define APIC_LVT_DEL_MODE_INIT       (0x5 << 8)
#define APIC_LVT_VECTOR_MASK         (0xFF)

#define APIC_REG_INIT_CNT (0x38)
#define APIC_REG_CURR_CNT (0x39)
#define APIC_REG_DCR      (0x3E)

// APIC_DCR_DIV_VALUE
#define APIC_DCR_DIV_1   (0b1011)
#define APIC_DCR_DIV_2   (0b0000)
#define APIC_DCR_DIV_4   (0b0001)
#define APIC_DCR_DIV_8   (0b0010)
#define APIC_DCR_DIV_16  (0b0011)
#define APIC_DCR_DIV_32  (0b1000)
#define APIC_DCR_DIV_64  (0b1001)
#define APIC_DCR_DIV_128 (0b1010)

#define APIC_REG_SELF_IPI (0x3F) /*only in x2APIC*/

#define APIC_REG_INDEX(reg_name) (APIC_REG_##reg_name)

#define xAPIC_MMIO_BASE               0xFEE00000
#define xAPIC_REG_PHY_ADDR(reg_index) (xAPIC_MMIO_BASE + reg_index * 0x10)
#define xAPIC_RD_REG(reg_name, vaddr_off)                                \
        (*((volatile u32 *)(xAPIC_REG_PHY_ADDR(APIC_REG_INDEX(reg_name)) \
                            + vaddr_off)))
#define xAPIC_WR_REG(reg_name, vaddr_off, value)                         \
        (*((volatile u32 *)(xAPIC_REG_PHY_ADDR(APIC_REG_INDEX(reg_name)) \
                            + vaddr_off)) = value)

#define x2APIC_MSR_BASE            0x800
#define x2APIC_REG_ADDR(reg_index) (x2APIC_MSR_BASE + reg_index)
#define x2APIC_RD_REG(reg_name, vaddr_off) \
        (rdmsr(x2APIC_REG_ADDR(APIC_REG_INDEX(reg_name))))
#define x2APIC_WR_REG(reg_name, vaddr_off, value) \
        (wrmsr(x2APIC_REG_ADDR(APIC_REG_INDEX(reg_name)), value))
#include <arch/x86_64/PIC/IRQ.h>
extern enum IRQ_type arch_irq_type;
#define APIC_RD_REG(reg_name, vaddr_off)                                    \
        ((arch_irq_type == xAPIC_IRQ) ? xAPIC_RD_REG(reg_name, vaddr_off) : \
                                        x2APIC_RD_REG(reg_name, vaddr_off))
#define APIC_WR_REG(reg_name, vaddr_off, value)             \
        ((arch_irq_type == xAPIC_IRQ) ?                     \
                 xAPIC_WR_REG(reg_name, vaddr_off, value) : \
                 x2APIC_WR_REG(reg_name, vaddr_off, value))
// this is used to r/w the 256bits, include isr tmr irr regs
enum lapic_vec_type {
        lapic_isr_type,
        lapic_tmr_type,
        lapic_irr_type,
};
bool lapic_get_vec(int bit, enum lapic_vec_type t);
void lapic_set_vec(int bit, enum lapic_vec_type t);
void lapci_clear_vec(int bit, enum lapic_vec_type t);

bool xAPIC_support(void);
bool x2APIC_support(void);
bool TSC_DDL_support(void);

void enable_xAPIC(void);
void enable_x2APIC(void);
void disable_APIC(void);

void reset_APIC(void);
void reset_xAPIC_LDR(void);

/*apic timer part*/
tick_t APIC_timer_calibration();
void APIC_timer_reset();
tick_t APIC_GET_HZ();
tick_t APIC_GET_CUR_TIME();

void software_enable_APIC(void);
bool map_LAPIC(void);
void APIC_EOI(void);

#endif