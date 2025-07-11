#include <arch/aarch64/sys_ctrl.h>
#include <arch/aarch64/trap/trap.h>
#include <modules/log/log.h>
#include <arch/aarch64/power_ctrl.h>

extern u64 trap_vec_table;
void arch_init_interrupt(void)
{
        set_vbar_el1((vaddr)(&trap_vec_table));
        __asm__ __volatile__("msr DAIFCLR,0x7");
}
void arch_unknown_trap_handler(struct trap_frame *tf)
{
        /*print the trap frames*/
        pr_info("arch unknown trap handler trap info 0x%x\n", tf->trap_info);
        pr_info("x0\t:\t0x%x\n", tf->REGS[0]);
        pr_info("x1\t:\t0x%x\n", tf->REGS[1]);
        pr_info("x2\t:\t0x%x\n", tf->REGS[2]);
        pr_info("x3\t:\t0x%x\n", tf->REGS[3]);

        pr_info("x4\t:\t0x%x\n", tf->REGS[4]);
        pr_info("x5\t:\t0x%x\n", tf->REGS[5]);
        pr_info("x6\t:\t0x%x\n", tf->REGS[6]);
        pr_info("x7\t:\t0x%x\n", tf->REGS[7]);

        pr_info("x8\t:\t0x%x\n", tf->REGS[8]);
        pr_info("x9\t:\t0x%x\n", tf->REGS[9]);
        pr_info("x10\t:\t0x%x\n", tf->REGS[10]);
        pr_info("x11\t:\t0x%x\n", tf->REGS[11]);

        pr_info("x12\t:\t0x%x\n", tf->REGS[12]);
        pr_info("x13\t:\t0x%x\n", tf->REGS[13]);
        pr_info("x14\t:\t0x%x\n", tf->REGS[14]);
        pr_info("x15\t:\t0x%x\n", tf->REGS[15]);

        pr_info("x16\t:\t0x%x\n", tf->REGS[16]);
        pr_info("x17\t:\t0x%x\n", tf->REGS[17]);
        pr_info("x18\t:\t0x%x\n", tf->REGS[18]);
        pr_info("x19\t:\t0x%x\n", tf->REGS[19]);

        pr_info("x20\t:\t0x%x\n", tf->REGS[20]);
        pr_info("x21\t:\t0x%x\n", tf->REGS[21]);
        pr_info("x22\t:\t0x%x\n", tf->REGS[22]);
        pr_info("x23\t:\t0x%x\n", tf->REGS[23]);

        pr_info("x24\t:\t0x%x\n", tf->REGS[24]);
        pr_info("x25\t:\t0x%x\n", tf->REGS[25]);
        pr_info("x26\t:\t0x%x\n", tf->REGS[26]);
        pr_info("x27\t:\t0x%x\n", tf->REGS[27]);

        pr_info("x28\t:\t0x%x\n", tf->REGS[28]);
        pr_info("x29\t:\t0x%x\n", tf->REGS[29]);
        pr_info("x30\t:\t0x%x\n", tf->REGS[30]);

        pr_info("spsr\t:\t0x%x\n", tf->SPSR);
        pr_info("elr\t:\t0x%x\n", tf->ELR);
        pr_info("SP\t:\t0x%x\n", tf->SP);
        pr_info("esr\t:\t0x%x\n", tf->ESR);

        pr_info("far\t:\t0x%x\n", tf->FAR);
}
void arch_eoi_irq(u64 trap_info)
{
        union irq_source source = {.irq_source_value = trap_info};
        source.irq_id = TRAP_SRC(source.irq_id);
        source.irq_id = AARCH64_TRAP_ID_TO_IRQ(source.irq_id);
        gic.eoi(source);
}

void get_curr_el_trap_info(struct trap_frame *tf)
{
        switch (tf->trap_info) {
        case TRAP_TYPE_SYNC:
                tf->trap_info = AARCH64_ESR_GET_EC(tf->ESR);
                break;
        case TRAP_TYPE_IRQ:
                union irq_source source = gic.read_irq_num();
                tf->trap_info = AARCH64_IRQ_TO_TRAP_ID(source.irq_id);
                break;
        case TRAP_TYPE_FIQ:
                break;
        default:
                break;
        }
        tf->trap_info |= AARCH64_TRAP_SRC_EL_1 << AARCH64_TRAP_SRC_EL_SHIFT;
}