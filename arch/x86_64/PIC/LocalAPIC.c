#include <arch/x86_64/PIC/APIC.h>
#include <shampoos/mm/map_handler.h>
#include <arch/x86_64/PIC/IRQ.h>
#include <common/types.h>
#include <shampoos/mm/vmm.h>
#include <modules/log/log.h>
#include <common/bit.h>
extern struct map_handler Map_Handler;
extern int arch_irq_type;
void reset_xAPIC(void)
{
        xAPIC_WR_REG(DFR, KERNEL_VIRT_OFFSET, 0xFFFFFFFF);
        u32 ldr_val = xAPIC_RD_REG(LDR, KERNEL_VIRT_OFFSET);
        ldr_val = set_mask(ldr_val, 0xFFFFFF);
        ldr_val = ldr_val | 1;
        xAPIC_WR_REG(LDR, KERNEL_VIRT_OFFSET, ldr_val);
        xAPIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, APIC_LVT_MASKED);
        xAPIC_WR_REG(LVT_PERF, KERNEL_VIRT_OFFSET, APIC_LVT_DEL_MODE_NMI);
        xAPIC_WR_REG(LVT_LINT_0, KERNEL_VIRT_OFFSET, APIC_LVT_MASKED);
        xAPIC_WR_REG(LVT_LINT_0, KERNEL_VIRT_OFFSET, APIC_LVT_MASKED);
        xAPIC_WR_REG(TPR, KERNEL_VIRT_OFFSET, 0);
}
void reset_x2APIC(void)
{
        // DFR is not used in x2APIC mode, and LDR is read only
        x2APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, APIC_LVT_MASKED);
        x2APIC_WR_REG(LVT_PERF, KERNEL_VIRT_OFFSET, APIC_LVT_DEL_MODE_NMI);
        x2APIC_WR_REG(LVT_LINT_0, KERNEL_VIRT_OFFSET, APIC_LVT_MASKED);
        x2APIC_WR_REG(LVT_LINT_0, KERNEL_VIRT_OFFSET, APIC_LVT_MASKED);
        x2APIC_WR_REG(TPR, KERNEL_VIRT_OFFSET, 0);
}
bool map_LAPIC(void)
{
        if (arch_irq_type != xAPIC_IRQ)
                return false; /*only xAPIC need mmio*/
        paddr vspace_root = get_current_kernel_vspace_root();
        paddr lapic_phy_page = xAPIC_MMIO_BASE;
        vaddr lapic_virt_page = KERNEL_PHY_TO_VIRT(xAPIC_MMIO_BASE);
        if (map(&vspace_root,
                PPN(lapic_phy_page),
                VPN(lapic_virt_page),
                3,
                PAGE_ENTRY_UNCACHED, // the LAPIC should be set as uncached
                &Map_Handler)) {
                pr_error("[ LAPIC ] ERROR: map error\n");
                return false;
        }
        return true;
}
u64 isr_reg[8] = {
        APIC_REG_ISR_0,
        APIC_REG_ISR_1,
        APIC_REG_ISR_2,
        APIC_REG_ISR_3,
        APIC_REG_ISR_4,
        APIC_REG_ISR_5,
        APIC_REG_ISR_6,
        APIC_REG_ISR_7,
};
u64 tmr_reg[8] = {
        APIC_REG_TMR_0,
        APIC_REG_TMR_1,
        APIC_REG_TMR_2,
        APIC_REG_TMR_3,
        APIC_REG_TMR_4,
        APIC_REG_TMR_5,
        APIC_REG_TMR_6,
        APIC_REG_TMR_7,
};
u64 irr_reg[8] = {
        APIC_REG_IRR_0,
        APIC_REG_IRR_1,
        APIC_REG_IRR_2,
        APIC_REG_IRR_3,
        APIC_REG_IRR_4,
        APIC_REG_IRR_5,
        APIC_REG_IRR_6,
        APIC_REG_IRR_7,
};
bool lapic_get_vec(int bit, enum lapic_vec_type t)
{
        if (!(bit >= 0 && bit <= 127))
                return 0;
        int index = bit / APIC_REG_SIZE;
        int offset = bit % APIC_REG_SIZE;
        u32 reg_val = 0;

        if (arch_irq_type == xAPIC_IRQ) {
                if (t == lapic_isr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(isr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                } else if (t == lapic_tmr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(tmr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                } else if (t == lapic_irr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(irr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                } else {
                        return 0;
                }
        } else if (arch_irq_type == x2APIC_IRQ) {
                if (t == lapic_isr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(isr_reg[index]));
                } else if (t == lapic_tmr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(tmr_reg[index]));
                } else if (t == lapic_irr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(irr_reg[index]));
                } else {
                        return 0;
                }
        }
        return (reg_val & (1 << offset)) != 0;
}
void lapic_set_vec(int bit, enum lapic_vec_type t)
{
        if (!(bit >= 0 && bit <= 127))
                return;
        int index = bit / APIC_REG_SIZE;
        int offset = bit % APIC_REG_SIZE;
        u32 reg_val = 0;
        if (arch_irq_type == xAPIC_IRQ) {
                if (t == lapic_isr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(isr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                        reg_val = set_bit(reg_val, offset);
                        (*((u32 *)(xAPIC_REG_PHY_ADDR(isr_reg[index])
                                   + KERNEL_VIRT_OFFSET))) = reg_val;
                } else if (t == lapic_tmr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(tmr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                        reg_val = set_bit(reg_val, offset);
                        (*((u32 *)(xAPIC_REG_PHY_ADDR(tmr_reg[index])
                                   + KERNEL_VIRT_OFFSET))) = reg_val;
                } else if (t == lapic_irr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(irr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                        reg_val = set_bit(reg_val, offset);
                        (*((u32 *)(xAPIC_REG_PHY_ADDR(irr_reg[index])
                                   + KERNEL_VIRT_OFFSET))) = reg_val;
                } else {
                        return;
                }
        } else if (arch_irq_type == x2APIC_IRQ) {
                if (t == lapic_isr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(isr_reg[index]));
                        reg_val = set_bit(reg_val, offset);
                        wrmsr(x2APIC_REG_ADDR(isr_reg[index]), reg_val);
                } else if (t == lapic_tmr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(tmr_reg[index]));
                        reg_val = set_bit(reg_val, offset);
                        wrmsr(x2APIC_REG_ADDR(tmr_reg[index]), reg_val);
                } else if (t == lapic_irr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(irr_reg[index]));
                        reg_val = set_bit(reg_val, offset);
                        wrmsr(x2APIC_REG_ADDR(irr_reg[index]), reg_val);
                } else {
                        return;
                }
        }
}
void lapci_clear_vec(int bit, enum lapic_vec_type t)
{
        if (!(bit >= 0 && bit <= 127))
                return;
        int index = bit / APIC_REG_SIZE;
        int offset = bit % APIC_REG_SIZE;
        u32 reg_val = 0;
        if (arch_irq_type == xAPIC_IRQ) {
                if (t == lapic_isr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(isr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                        reg_val = clear_bit(reg_val, offset);
                        (*((u32 *)(xAPIC_REG_PHY_ADDR(isr_reg[index])
                                   + KERNEL_VIRT_OFFSET))) = reg_val;
                } else if (t == lapic_tmr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(tmr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                        reg_val = clear_bit(reg_val, offset);
                        (*((u32 *)(xAPIC_REG_PHY_ADDR(tmr_reg[index])
                                   + KERNEL_VIRT_OFFSET))) = reg_val;
                } else if (t == lapic_irr_type) {
                        reg_val = (*((u32 *)(xAPIC_REG_PHY_ADDR(irr_reg[index])
                                             + KERNEL_VIRT_OFFSET)));
                        reg_val = clear_bit(reg_val, offset);
                        (*((u32 *)(xAPIC_REG_PHY_ADDR(irr_reg[index])
                                   + KERNEL_VIRT_OFFSET))) = reg_val;
                } else {
                        return;
                }
        } else if (arch_irq_type == x2APIC_IRQ) {
                if (t == lapic_isr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(isr_reg[index]));
                        reg_val = clear_bit(reg_val, offset);
                        wrmsr(x2APIC_REG_ADDR(isr_reg[index]), reg_val);
                } else if (t == lapic_tmr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(tmr_reg[index]));
                        reg_val = clear_bit(reg_val, offset);
                        wrmsr(x2APIC_REG_ADDR(tmr_reg[index]), reg_val);
                } else if (t == lapic_irr_type) {
                        reg_val = rdmsr(x2APIC_REG_ADDR(irr_reg[index]));
                        reg_val = clear_bit(reg_val, offset);
                        wrmsr(x2APIC_REG_ADDR(irr_reg[index]), reg_val);
                } else {
                        return;
                }
        }
}