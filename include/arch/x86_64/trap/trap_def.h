/*
 * x86_64 trap macro definitions.
 *
 * This file contains ONLY macro definitions (no structures).
 * May be included by both C and assembly code.
 *
 * Trap vector numbers: use enum TRAP_NUM from trap.h
 * This file defines only PF error code bits and other constants.
 */
#ifndef _X86_64_TRAP_DEF_H_
#define _X86_64_TRAP_DEF_H_

/* Page Fault error code bits (Intel SDM Vol. 3A 6.15) */
#define X86_PF_EC_PRESENT                                   \
        0x1 /* bit 0: 0=not present, 1=protection violation \
             */
#define X86_PF_EC_WRITE    0x2 /* bit 1: 0=read, 1=write */
#define X86_PF_EC_USER     0x4 /* bit 2: 0=kernel, 1=user */
#define X86_PF_EC_RESERVED 0x8 /* bit 3: reserved */
#define X86_PF_EC_INSTR    0x10 /* bit 4: 1=instruction fetch */
#define X86_PF_EC_PK       0x20 /* bit 5: protection key violation */
#define X86_PF_EC_SS       0x40 /* bit 6: shadow stack access */
#define X86_PF_EC_HV       0x80 /* bit 7: hypervisor MMIO */

#endif
