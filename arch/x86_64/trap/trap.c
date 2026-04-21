#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/trap/trap.h>
#include <arch/x86_64/power_ctrl.h>
#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/trap/tss.h>
#include <modules/log/log.h>
#include <common/string.h>

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
                             RSP0_INT_NUM);
        }
        lidt(&idtr_desc);
}

void arch_unknown_trap_handler(struct trap_frame *tf)
{
        pr_info("trap_info\t:\t0x%lx\n", tf->trap_info);
        pr_info("ss\t:\t0x%lx\n", tf->ss);
        pr_info("rsp\t:\t0x%lx\n", tf->rsp);
        pr_info("eflags\t:\t0x%lx\n", tf->eflags);
        pr_info("cs\t:\t0x%lx\n", tf->cs);
        pr_info("rip\t:\t0x%lx\n", tf->rip);
        pr_info("e code\t:\t0x%lx\n", tf->error_code);

        pr_info("rdi\t:\t0x%lx\n", tf->rdi);
        pr_info("rsi\t:\t0x%lx\n", tf->rsi);
        pr_info("rdx\t:\t0x%lx\n", tf->rdx);
        pr_info("rcx\t:\t0x%lx\n", tf->rcx);
        pr_info("rax\t:\t0x%lx\n", tf->rax);
        pr_info("r8\t:\t0x%lx\n", tf->r8);
        pr_info("r9\t:\t0x%lx\n", tf->r9);
        pr_info("r10\t:\t0x%lx\n", tf->r10);
        pr_info("r11\t:\t0x%lx\n", tf->r11);

        pr_info("rbx\t:\t0x%lx\n", tf->rbx);
        pr_info("rbp\t:\t0x%lx\n", tf->rbp);
        pr_info("r12\t:\t0x%lx\n", tf->r12);
        pr_info("r13\t:\t0x%lx\n", tf->r13);
        pr_info("r14\t:\t0x%lx\n", tf->r14);
        pr_info("r15\t:\t0x%lx\n", tf->r15);
}

void arch_eoi_irq(u64 trap_info)
{
        if (arch_irq_type == PIC_IRQ) {
                PIC_EOI(TRAP_ID(trap_info));
        } else if (arch_irq_type == xAPIC_IRQ) {
                APIC_EOI();
        }
}
/* Mapping table: x86_64 trap ID -> trap_class */
static const enum trap_class x86_trap_class_map[TRAP_ARCH_USED] = {
        [TRAP_DE] = TRAP_CLASS_DIVIDE_ERROR,
        [TRAP_DB] = TRAP_CLASS_DEBUG, /* Debug exception */
        [TRAP_NMI] = TRAP_CLASS_IRQ,
        [TRAP_BP] = TRAP_CLASS_BREAKPOINT,
        [TRAP_OF] = TRAP_CLASS_OVERFLOW,
        [TRAP_BR] = TRAP_CLASS_UNKNOWN, /* BOUND instruction (obsolete) */
        [TRAP_UD] = TRAP_CLASS_ILLEGAL_INSTR,
        [TRAP_NM] = TRAP_CLASS_FP_FAULT,
        [TRAP_DF] = TRAP_CLASS_DOUBLE_FAULT, /* Double fault */
        [TRAP_CSO] = TRAP_CLASS_UNKNOWN, /* Coprocessor Segment Overrun
                                            (obsolete) */
        [TRAP_TS] = TRAP_CLASS_SEGMENT_FAULT, /* Invalid TSS */
        [TRAP_NP] = TRAP_CLASS_SEGMENT_FAULT, /* Segment Not Present */
        [TRAP_SS] = TRAP_CLASS_STACK_FAULT,
        [TRAP_GP] = TRAP_CLASS_GP_FAULT,
        [TRAP_PF] = TRAP_CLASS_PAGE_FAULT,
        [TRAP_MF] = TRAP_CLASS_FP_FAULT,
        [TRAP_AC] = TRAP_CLASS_ALIGNMENT,
        [TRAP_MC] = TRAP_CLASS_MACHINE_CHECK,
        [TRAP_XM] = TRAP_CLASS_FP_FAULT,
        [TRAP_VE] = TRAP_CLASS_VIRTUALIZATION, /* Virtualization Exception */
        [TRAP_SE] = TRAP_CLASS_SECURITY, /* Security Exception */
};

/* Fixed trap handlers indexed by trap_class */
static fixed_trap_handler_t fixed_trap_handlers[TRAP_CLASS_UNKNOWN];
static u64 fixed_trap_attrs[TRAP_CLASS_UNKNOWN];

/*
 * Wrapper to convert irq_handler call to fixed_trap_handler call.
 */
static void x86_fixed_trap_wrapper(struct trap_frame *tf)
{
        struct x86_64_trap_info info;
        arch_populate_trap_info(tf, &info);

        enum trap_class trap_class = info.trap_class;
        if (trap_class < TRAP_CLASS_UNKNOWN
            && fixed_trap_handlers[trap_class]) {
                fixed_trap_handlers[trap_class](tf);
        }
}

/*
 * @brief Populate x86_64 trap information (separate structure)
 */
void arch_populate_trap_info(struct trap_frame *tf,
                             struct x86_64_trap_info *info)
{
        u64 trap_id = TRAP_ID(tf->trap_info);

        memset(info, 0, sizeof(*info));

        /* Store reference to trap_frame */
        info->tf = tf;

        /* Fill basic information */
        info->is_user = !arch_int_from_kernel(tf);
        info->error_code = tf->error_code;

        /* Map to trap_class */
        if (trap_id < TRAP_ARCH_USED) {
                info->trap_class = x86_trap_class_map[trap_id];
        } else if (trap_id >= 32) {
                info->trap_class = TRAP_CLASS_IRQ;
        } else {
                info->trap_class = TRAP_CLASS_UNKNOWN;
        }

        /* Fill detailed information based on trap_class */
        switch (info->trap_class) {
        case TRAP_CLASS_PAGE_FAULT:
                info->fault_addr = arch_get_fault_addr(tf);
                info->cr2 = info->fault_addr;

                /* Parse PF error code */
                info->pf_ec.page_present = (tf->error_code & X86_PF_EC_PRESENT)
                                           != 0;
                info->pf_ec.write_access = (tf->error_code & X86_PF_EC_WRITE)
                                           != 0;
                info->pf_ec.user_mode = (tf->error_code & X86_PF_EC_USER) != 0;
                info->pf_ec.reserved_bit = (tf->error_code & X86_PF_EC_RESERVED)
                                           != 0;
                info->pf_ec.instruction_fetch =
                        (tf->error_code & X86_PF_EC_INSTR) != 0;
                info->pf_ec.protection_key = (tf->error_code & X86_PF_EC_PK)
                                             != 0;
                info->pf_ec.shadow_stack = (tf->error_code & X86_PF_EC_SS) != 0;
                info->pf_ec.hv_mmio = (tf->error_code & X86_PF_EC_HV) != 0;

                /* Sync to TRAP_COMMON */
                info->is_write = info->pf_ec.write_access;
                info->is_execute = info->pf_ec.instruction_fetch;
                info->is_present = info->pf_ec.page_present;
                info->is_fatal = false;
                break;

        case TRAP_CLASS_ILLEGAL_INSTR:
        case TRAP_CLASS_DIVIDE_ERROR:
        case TRAP_CLASS_GP_FAULT:
        case TRAP_CLASS_MACHINE_CHECK:
        case TRAP_CLASS_STACK_FAULT:
        case TRAP_CLASS_DOUBLE_FAULT: /* Double fault is fatal */
        case TRAP_CLASS_SEGMENT_FAULT: /* Segment faults are typically fatal */
                info->is_fatal = true;
                break;

        case TRAP_CLASS_ALIGNMENT:
        case TRAP_CLASS_FP_FAULT:
        case TRAP_CLASS_DEBUG: /* Debug exceptions are recoverable */
        case TRAP_CLASS_SECURITY: /* Security exceptions need handling */
        case TRAP_CLASS_VIRTUALIZATION: /* Virtualization exceptions need
                                           handling */
                info->is_fatal = false;
                break;

        default:
                break;
        }

        info->arch_flags = tf->error_code;
}

/*
 * @brief Register fixed trap handler (x86_64 implementation)
 */
void register_fixed_trap(enum trap_class trap_class,
                         fixed_trap_handler_t handler, u64 irq_attr)
{
        if (trap_class >= TRAP_CLASS_UNKNOWN) {
                return;
        }

        /* Save handler and attr */
        fixed_trap_handlers[trap_class] = handler;
        fixed_trap_attrs[trap_class] = irq_attr;

        /* Reverse mapping: find corresponding x86 trap ID(s) */
        for (int trap_id = 0; trap_id < TRAP_ARCH_USED; trap_id++) {
                if (x86_trap_class_map[trap_id] == trap_class) {
                        register_irq_handler(
                                trap_id, x86_fixed_trap_wrapper, irq_attr);
                }
        }
}
