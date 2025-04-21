#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/trap/trap.h>
#include <arch/x86_64/power_ctrl.h>
#include <modules/log/log.h>
#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/trap/tss.h>

extern u64 trap_vec;
extern enum IRQ_type arch_irq_type;
extern union idt_gate_desc trap_vec_table[NR_IRQ];
const char *trap_name_string[TRAP_ARCH_USED + 2] = {
        "Fault:Divide Error\n\0",
        "Fault/Trap:Debug Exception\n\0",
        "Interrupt:NMI Interrupt\n\0",
        "Trap:Breakpoint\n\0",
        "Trap:Overflow\n\0",
        "Fault:BOUND Range exceeded\n\0",
        "Fault:Invalid Opcode\n\0",
        "Fault:Math Coprocessor Device Not Available\n\0",
        "Abort:Double Fault\n\0",
        "Fault:Coprocessor Segment Overrun\n\0",
        "Fault:Invalid TSS\n\0",
        "Fault:Segment Not Present\n\0",
        "Fault:Stack-Segment Fault\n\0",
        "Fault:General Protection\n\0",
        "Fault:Page Fault\n\0",
        "Intel reserved\n\0",
        "Fault:x87 FPU Floating-Point Error\n\0",
        "Fault:Alignment Check\n\0",
        "Abort:Machine Check\n\0",
        "Fault:SIMD Floating-Point Exception\n\0",
        "Fault:Virtualization Exception\n\0",
        /*others*/
        "Intel reserved\n\0",
        /*for trap number between 21-31*/
        "User Defined interrupts\n\0"};
void arch_init_interrupt(void)
{
        struct pseudo_descriptor idtr_desc;
        union desc_selector sel;

        idtr_desc.limit = NR_IRQ * sizeof(union idt_gate_desc) - 1;
        idtr_desc.base_addr = (u64)(&trap_vec_table);
        for (int i = 0; i < NR_IRQ; ++i) {
                /*generate the IDT table*/
                sel.rpl = KERNEL_PL;
                sel.table_indicator = 0;
                sel.index = 1;
                SET_IDT_GATE(trap_vec_table[i],
                             (vaddr)((u64 *)&trap_vec)[i],
                             sel,
                             KERNEL_PL,
                             IA32E_IDT_GATE_TYPE_INT,
                             IST_INT_NUM);
        }
        lidt(&idtr_desc);
}

void arch_unknown_trap_handler(struct trap_frame *tf)
{
		pr_info("trap_info\t:\t0x%x\n",tf->trap_info);
        pr_info("ss\t:\t0x%x\n", tf->ss);
        pr_info("rsp\t:\t0x%x\n", tf->rsp);
        pr_info("eflags\t:\t0x%x\n", tf->eflags);
        pr_info("cs\t:\t0x%x\n", tf->cs);
        pr_info("rip\t:\t0x%x\n", tf->rip);
        pr_info("e code\t:\t0x%x\n", tf->error_code);

        pr_info("rdi\t:\t0x%x\n", tf->rdi);
        pr_info("rsi\t:\t0x%x\n", tf->rsi);
        pr_info("rdx\t:\t0x%x\n", tf->rdx);
        pr_info("rcx\t:\t0x%x\n", tf->rcx);
        pr_info("rax\t:\t0x%x\n", tf->rax);
        pr_info("r8\t:\t0x%x\n", tf->r8);
        pr_info("r9\t:\t0x%x\n", tf->r9);
        pr_info("r10\t:\t0x%x\n", tf->r10);
        pr_info("r11\t:\t0x%x\n", tf->r11);

        pr_info("rbx\t:\t0x%x\n", tf->rbx);
        pr_info("rbp\t:\t0x%x\n", tf->rbp);
        pr_info("r12\t:\t0x%x\n", tf->r12);
        pr_info("r13\t:\t0x%x\n", tf->r13);
        pr_info("r14\t:\t0x%x\n", tf->r14);
        pr_info("r15\t:\t0x%x\n", tf->r15);
        arch_shutdown();
}

void arch_eoi_irq(u64 trap_info)
{
        if (arch_irq_type == PIC_IRQ) {
                PIC_EOI(TRAP_ID(trap_info));
        } else if (arch_irq_type == xAPIC_IRQ) {
                APIC_EOI();
        }
}