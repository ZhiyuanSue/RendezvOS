#include <rendezvos/trap.h>
#include <arch/aarch64/sys_ctrl.h>
#include <arch/aarch64/trap/trap.h>
#include <modules/log/log.h>
#include <arch/aarch64/power_ctrl.h>
#include <common/string.h>

extern u64 trap_vec_table;
void arch_init_interrupt(void)
{
        set_vbar_el1((vaddr)(&trap_vec_table));
        __asm__ __volatile__("msr DAIFCLR,0x7");
}
void arch_unknown_trap_handler(struct trap_frame *tf)
{
        /*print the trap frames*/
        pr_info("arch unknown trap handler trap info 0x%lx\n", tf->trap_info);
        pr_info("x0\t:\t0x%lx\n", tf->REGS[0]);
        pr_info("x1\t:\t0x%lx\n", tf->REGS[1]);
        pr_info("x2\t:\t0x%lx\n", tf->REGS[2]);
        pr_info("x3\t:\t0x%lx\n", tf->REGS[3]);

        pr_info("x4\t:\t0x%lx\n", tf->REGS[4]);
        pr_info("x5\t:\t0x%lx\n", tf->REGS[5]);
        pr_info("x6\t:\t0x%lx\n", tf->REGS[6]);
        pr_info("x7\t:\t0x%lx\n", tf->REGS[7]);

        pr_info("x8\t:\t0x%lx\n", tf->REGS[8]);
        pr_info("x9\t:\t0x%lx\n", tf->REGS[9]);
        pr_info("x10\t:\t0x%lx\n", tf->REGS[10]);
        pr_info("x11\t:\t0x%lx\n", tf->REGS[11]);

        pr_info("x12\t:\t0x%lx\n", tf->REGS[12]);
        pr_info("x13\t:\t0x%lx\n", tf->REGS[13]);
        pr_info("x14\t:\t0x%lx\n", tf->REGS[14]);
        pr_info("x15\t:\t0x%lx\n", tf->REGS[15]);

        pr_info("x16\t:\t0x%lx\n", tf->REGS[16]);
        pr_info("x17\t:\t0x%lx\n", tf->REGS[17]);
        pr_info("x18\t:\t0x%lx\n", tf->REGS[18]);
        pr_info("x19\t:\t0x%lx\n", tf->REGS[19]);

        pr_info("x20\t:\t0x%lx\n", tf->REGS[20]);
        pr_info("x21\t:\t0x%lx\n", tf->REGS[21]);
        pr_info("x22\t:\t0x%lx\n", tf->REGS[22]);
        pr_info("x23\t:\t0x%lx\n", tf->REGS[23]);

        pr_info("x24\t:\t0x%lx\n", tf->REGS[24]);
        pr_info("x25\t:\t0x%lx\n", tf->REGS[25]);
        pr_info("x26\t:\t0x%lx\n", tf->REGS[26]);
        pr_info("x27\t:\t0x%lx\n", tf->REGS[27]);

        pr_info("x28\t:\t0x%lx\n", tf->REGS[28]);
        pr_info("x29\t:\t0x%lx\n", tf->REGS[29]);
        pr_info("x30\t:\t0x%lx\n", tf->REGS[30]);

        pr_info("spsr\t:\t0x%lx\n", tf->SPSR);
        pr_info("elr\t:\t0x%lx\n", tf->ELR);
        pr_info("SP\t:\t0x%lx\n", tf->SP);
        pr_info("esr\t:\t0x%lx\n", tf->ESR);

        pr_info("far\t:\t0x%lx\n", tf->FAR);
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
};

/*
 * Mapping: aarch64 EC (Exception Class) -> trap_class
 *
 * EC is in ESR[31:26], range 0x00-0x3F (6 bits).
 * This function maps EC values ONLY - not DFSC/IFSC (which are in ISS[5:0]).
 *
 * DFSC/IFSC parsing is done separately in arch_populate_trap_info().
 *
 * References:
 * - ARM DDI0487 (ARM ARM)
 * - Linux kernel arch/arm64/include/asm/esr.h
 * - Linux kernel arch/arm64/kernel/traps.c
 *
 * EC Value Verification Table:
 *
 * | EC  | Name                          | Source          | trap_class |
 * Verified |
 * |-----|-------------------------------|-----------------|----------------------|----------|
 * | 0x00| Unknown reason                | ARM ARM         | UNKNOWN | ✅ | |
 * 0x01| Trapped WFI/WFE               | ARM ARM         | UNKNOWN | ✅       |
 * | 0x02| Trapped MRS/MSR               | ARM ARM         | ILLEGAL_INSTR | ✅
 * | | 0x03| Trapped CP15 MRC/MCR         | ARM ARM         | ILLEGAL_INSTR | ✅
 * | | 0x04| Trapped CP14 MRRC/MCRR       | ARM ARM         | ILLEGAL_INSTR | ✅
 * | | 0x05| Trapped SVE/SIMD/FP          | ARM ARM         | ILLEGAL_INSTR | ✅
 * | | 0x06| Trapped other instructions   | ARM ARM         | ILLEGAL_INSTR | ✅
 * | | 0x07| FP access trap                | ARM ARM         | FP_FAULT | ✅ |
 * | 0x08| FP exception                  | ARM ARM         | FP_FAULT | ✅ | |
 * 0x09| (not an EC value)            | -               | -                    |
 * ❌       | | 0x0a| (not an EC value)            | -               | - | ❌ |
 * | 0x0b| (not an EC value)            | -               | - | ❌       | |
 * 0x0c| (not an EC value)            | -               | -                    |
 * ❌       | | 0x0d| Alignment fault               | ARM ARM         |
 * ALIGNMENT            | ✅       | | 0x0e| Illegal execution state       | ARM
 * ARM         | ILLEGAL_INSTR        | ✅       | | 0x0f| SError from lower EL
 * | ARM ARM         | ASYNC_ABORT          | ✅       | | 0x10| (not an EC
 * value)            | -               | -                    | ❌       | |
 * 0x11| Tag check fault (MTE)        | ARM ARM         | SECURITY             |
 * ✅       | | 0x15| SVC from lower EL            | ARM ARM         | SYSCALL
 * | ✅       | | 0x18| SVC in AArch64               | ARM ARM         | SYSCALL
 * | ✅       | | 0x20| Instruction abort (lower EL) | ARM ARM         |
 * PAGE_FAULT           | ✅       | | 0x21| Instruction abort (same EL)  | ARM
 * ARM         | PAGE_FAULT           | ✅       | | 0x22| PC alignment fault |
 * ARM ARM         | ALIGNMENT            | ✅       | | 0x24| Data abort (lower
 * EL)        | ARM ARM         | PAGE_FAULT           | ✅       | | 0x25| Data
 * abort (same EL)        | ARM ARM         | PAGE_FAULT           | ✅       |
 * | 0x26| SP alignment fault           | ARM ARM         | ALIGNMENT | ✅ | |
 * 0x2c| FP exception (AArch32)      | ARM ARM         | FP_FAULT             |
 * ✅       | | 0x2f| SError interrupt            | ARM ARM         |
 * ASYNC_ABORT          | ✅       | | 0x30| Breakpoint (lower EL)       | ARM
 * ARM         | BREAKPOINT           | ✅       | | 0x31| Breakpoint (same EL)
 * | ARM ARM         | BREAKPOINT           | ✅       | | 0x32| Software step
 * | ARM ARM         | DEBUG                | ✅       | | 0x33| Watchpoint |
 * ARM ARM         | DEBUG                | ✅       | | 0x34| Vector catch |
 * ARM ARM         | DEBUG                | ✅       | | 0x35| Other debug
 * exceptions       | ARM ARM         | DEBUG                | ✅       | |
 * 0x3c| Software step (BKPT)        | ARM ARM         | BREAKPOINT           |
 * ✅       |
 *
 * Note: 0x09, 0x0a, 0x0b, 0x0c are DFSC/IFSC codes, NOT EC values.
 */
static enum trap_class aarch64_ec_to_trap_class(u64 ec)
{
        switch (ec) {
        /* Uncategorized and reason-specific exceptions */
        case 0x00:
                return TRAP_CLASS_UNKNOWN; /* Unknown reason */
        case 0x01:
                return TRAP_CLASS_UNKNOWN; /* Trapped WFI/WFE */

        /* Trapped instruction execution */
        case 0x02:
                return TRAP_CLASS_ILLEGAL_INSTR; /* Trapped MRS/MSR */
        case 0x03:
                return TRAP_CLASS_ILLEGAL_INSTR; /* Trapped CP15 MRC/MCR */
        case 0x04:
                return TRAP_CLASS_ILLEGAL_INSTR; /* Trapped CP14 MRRC/MCRR */
        case 0x05:
                return TRAP_CLASS_ILLEGAL_INSTR; /* Trapped SVE/SIMD/FP */
        case 0x06:
                return TRAP_CLASS_ILLEGAL_INSTR; /* Trapped other instructions
                                                  */

        /* Floating-point and Advanced SIMD exceptions */
        case 0x07:
                return TRAP_CLASS_FP_FAULT; /* FP access trap */
        case 0x08:
                return TRAP_CLASS_FP_FAULT; /* FP exception */
        case 0x2c:
                return TRAP_CLASS_FP_FAULT; /* FP exception (AArch32) */

        /* Memory access faults */
        case 0x0d:
                return TRAP_CLASS_ALIGNMENT; /* Alignment fault */
        case 0x0e:
                return TRAP_CLASS_ILLEGAL_INSTR; /* Illegal execution state */
        case 0x11:
                return TRAP_CLASS_SECURITY; /* Tag check fault (MTE) */

        /* Instruction aborts (page faults) */
        case 0x20:
                return TRAP_CLASS_PAGE_FAULT; /* Instruction abort from lower EL
                                               */
        case 0x21:
                return TRAP_CLASS_PAGE_FAULT; /* Instruction abort from same EL
                                               */
        case 0x22:
                return TRAP_CLASS_ALIGNMENT; /* PC alignment fault */

        /* Data aborts (page faults) */
        case 0x24:
                return TRAP_CLASS_PAGE_FAULT; /* Data abort from lower EL */
        case 0x25:
                return TRAP_CLASS_PAGE_FAULT; /* Data abort from same EL */
        case 0x26:
                return TRAP_CLASS_ALIGNMENT; /* SP alignment fault */

        /* System-related exceptions */
        case 0x15:
                return TRAP_CLASS_SYSCALL; /* SVC from lower EL (AArch64) -
                                              user->kernel syscall */
        case 0x18:
                return TRAP_CLASS_SYSCALL; /* SVC in AArch64 - same-EL SVC
                                              (defensive) */
        /*
         * Note: We map both EC 0x15 and 0x18 to SYSCALL for completeness:
         * - EC 0x15: Normal syscall path (SVC from EL0 to EL1)
         * - EC 0x18: Same-EL SVC (should not happen in normal operation, but we
         * handle it defensively)
         *
         * Most syscalls will use EC 0x15. EC 0x18 is a defensive catch for
         * erroneous same-EL SVC instructions (kernel bugs, compromised kernel,
         * etc.).
         */

        /* Debug exceptions */
        case 0x30:
                return TRAP_CLASS_BREAKPOINT; /* Software breakpoint from lower
                                                 EL */
        case 0x31:
                return TRAP_CLASS_BREAKPOINT; /* Software breakpoint from same
                                                 EL */
        case 0x32:
                return TRAP_CLASS_DEBUG; /* Software step */
        case 0x33:
                return TRAP_CLASS_DEBUG; /* Watchpoint */
        case 0x34:
                return TRAP_CLASS_DEBUG; /* Vector catch */
        case 0x35:
                return TRAP_CLASS_DEBUG; /* Other debug exceptions */
        case 0x3c:
                return TRAP_CLASS_BREAKPOINT; /* Software step (BKPT from
                                                 AArch32) */

        /* Asynchronous errors */
        case 0x0f:
                return TRAP_CLASS_ASYNC_ABORT; /* SError from lower EL */
        case 0x2f:
                return TRAP_CLASS_ASYNC_ABORT; /* SError interrupt */

        /* Uncategorized/Reserved EC values */
        default:
                return TRAP_CLASS_UNKNOWN;
        }
}

/* Fixed trap handlers indexed by trap_class */
static fixed_trap_handler_t fixed_trap_handlers[TRAP_CLASS_UNKNOWN];
static u64 fixed_trap_attrs[TRAP_CLASS_UNKNOWN];

/*
 * Wrapper to convert irq_handler call to fixed_trap_handler call.
 */
static void aarch64_fixed_trap_wrapper(struct trap_frame *tf)
{
        struct aarch64_trap_info info;
        arch_populate_trap_info(tf, &info);

        enum trap_class trap_class = info.trap_class;
        if (trap_class < TRAP_CLASS_UNKNOWN
            && fixed_trap_handlers[trap_class]) {
                fixed_trap_handlers[trap_class](tf);
        }
}

/*
 * @brief Populate aarch64 trap information (separate structure)
 */
void arch_populate_trap_info(struct trap_frame *tf,
                             struct aarch64_trap_info *info)
{
        u64 ec = TRAP_ID(tf->trap_info);

        memset(info, 0, sizeof(*info));

        /* Store reference to trap_frame */
        info->tf = tf;

        /* Fill basic information */
        info->is_user = !arch_int_from_kernel(tf);
        info->error_code = tf->ESR;

        /* Parse ESR register */
        info->esr_fields.ec = (tf->ESR >> AARCH64_ESR_EC_SHIFT) & 0x3F;
        info->esr_fields.iss = tf->ESR & 0x1FFFFFF;
        info->esr_fields.isv = (tf->ESR >> 24) & 0x1;

        /* Map to trap_class */
        info->trap_class = aarch64_ec_to_trap_class(ec);

        /* Fill detailed information based on trap_class */
        switch (info->trap_class) {
        case TRAP_CLASS_PAGE_FAULT: {
                bool is_inst_abort = (ec == 0x20 || ec == 0x21);

                info->fault_addr = arch_get_fault_addr(tf);

                /* Parse ISS */
                if (!is_inst_abort) {
                        /* Data abort: parse DFSC and WnR */
                        info->esr_fields.dfsc = info->esr_fields.iss & 0x3F;
                        info->esr_fields.wnR = (info->esr_fields.iss >> 6)
                                               & 0x1;

                        /*
                         * DFSC (Data Fault Status Code) encoding in ISS[5:0]:
                         *
                         * Translation faults (page NOT present):
                         *   0x04: Translation fault, level 0
                         *   0x05: Translation fault, level 1
                         *   0x06: Translation fault, level 2
                         *   0x07: Translation fault, level 3
                         *   0x2B: Translation fault, level -1 (FEAT_LPA2)
                         *
                         * Access flag faults (page present, but AF not set):
                         *   0x08: Access flag fault, level 0
                         *   0x09: Access flag fault, level 1
                         *   0x0A: Access flag fault, level 2
                         *   0x0B: Access flag fault, level 3
                         *
                         * Permission faults (page present, but insufficient
                         * permission): 0x0C: Permission fault, level 0 0x0D:
                         * Permission fault, level 1 0x0E: Permission fault,
                         * level 2 0x0F: Permission fault, level 3
                         *
                         * is_present logic:
                         *   - Translation faults → is_present = false (page not
                         * present)
                         *   - Access flag/Permission faults → is_present = true
                         * (page exists)
                         */
                        u8 dfsc = info->esr_fields.dfsc;
                        info->is_present = !((dfsc >= 0x04 && dfsc <= 0x07)
                                             || dfsc == 0x2B);
                } else {
                        /* Instruction abort: parse IFSC */
                        info->esr_fields.ifsc = info->esr_fields.iss & 0x3F;

                        /*
                         * IFSC (Instruction Fault Status Code) uses same
                         * encoding as DFSC. Translation faults (0x04-0x07,
                         * 0x2B) → page not present. Other faults (access flag,
                         * permission) → page present.
                         */
                        u8 ifsc = info->esr_fields.ifsc;
                        info->is_present = !((ifsc >= 0x04 && ifsc <= 0x07)
                                             || ifsc == 0x2B);
                }

                /* Sync to TRAP_COMMON */
                info->is_execute = is_inst_abort;
                info->is_write = !is_inst_abort && info->esr_fields.wnR;
                info->is_fatal = false;
                break;
        }

        case TRAP_CLASS_ILLEGAL_INSTR:
        case TRAP_CLASS_DIVIDE_ERROR:
        case TRAP_CLASS_GP_FAULT:
        case TRAP_CLASS_DOUBLE_FAULT:
        case TRAP_CLASS_SEGMENT_FAULT:
                info->is_fatal = true;
                break;

        case TRAP_CLASS_ALIGNMENT:
        case TRAP_CLASS_FP_FAULT:
        case TRAP_CLASS_DEBUG:
        case TRAP_CLASS_SECURITY:
        case TRAP_CLASS_VIRTUALIZATION:
        case TRAP_CLASS_ASYNC_ABORT:
                info->is_fatal = false;
                break;

        default:
                break;
        }

        info->arch_flags = tf->ESR;
}

/*
 * @brief Register fixed trap handler (aarch64 implementation)
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

        /* Reverse mapping: find corresponding EC value(s) */
        for (u64 ec = 0; ec < 0x40; ec++) {
                if (aarch64_ec_to_trap_class(ec) == trap_class) {
                        register_irq_handler(
                                ec, aarch64_fixed_trap_wrapper, irq_attr);
                }
        }
}
