#ifndef _SHAMPOOS_LOCAL_APIC_H_
# define _SHAMPOOS_LOCAL_APIC_H_

# define APIC_ID (0x2)
# define APIC_VERSION (0x3)
# define APIC_TPR (0x8)
# define APIC_PPR (0xA)
# define APIC_EOI (0xB)
# define APIC_LDR (0xD)
# define APIC_SVR (0xF)

# define APIC_ISR (0x10)
# define APIC_ISR_1 (0x11)
# define APIC_ISR_2 (0x12)
# define APIC_ISR_3 (0x13)
# define APIC_ISR_4 (0x14)
# define APIC_ISR_5 (0x15)
# define APIC_ISR_6 (0x16)
# define APIC_ISR_7 (0x17)

# define APIC_TMR (0x18)
# define APIC_TMR_1 (0x19)
# define APIC_TMR_2 (0x1A)
# define APIC_TMR_3 (0x1B)
# define APIC_TMR_4 (0x1C)
# define APIC_TMR_5 (0x1D)
# define APIC_TMR_6 (0x1E)
# define APIC_TMR_7 (0x1F)

# define APIC_IRR (0x20)
# define APIC_IRR_1 (0x21)
# define APIC_IRR_2 (0x22)
# define APIC_IRR_3 (0x23)
# define APIC_IRR_4 (0x24)
# define APIC_IRR_5 (0x25)
# define APIC_IRR_6 (0x26)
# define APIC_IRR_7 (0x27)

# define APIC_ESR (0x28)
# define APIC_LVT_CMCI (0x2F)
# define APIC_ICR (0x30)
# define APIC_ICR_HIGH (0x31) /*only used in xAPIC, no 831H MSR reg in x2APIC*/

# define APIC_LVT_TIME (0x32)
# define APIC_LVT_THER (0x33)
# define APIC_LVT_PERF (0x34)
# define APIC_LVT_LINT_0 (0x35)
# define APIC_LVT_LINT_1 (0x36)
# define APIC_LVT_ERR (0x37)
# define APIC_INIT_CNT (0x38)
# define APIC_CURR_CNT (0x39)
# define APIC_DCR (0x3E)
# define APIC_SELF_IPI (0x3F) /*only in x2APIC*/

# define APIC_REG_INDEX(reg_name) (APIC_##reg_name)

# define xAPIC_MMIO_BASE 0xFEE00000
# define xAPIC_REG_ADDR(reg_index) (xAPIC_MMIO_BASE + reg_index * 0x10)

# define x2APIC_MSR_BASE 0x800
# define x2APIC_REG_ADDR(reg_index) (x2APIC_MSR_BASE + reg_index)

#endif