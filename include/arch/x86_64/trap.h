#ifndef _SHAMPOOS_TRAP_H_
#define _SHAMPOOS_TRAP_H_
#include <common/types.h>
#define IDT_LIMIT 256
enum TRAP_NUM {
	TRAP_DE,
	TRAP_DB,
	TRAP_NMI,
	TRAP_BP,
	TRAP_OF,
	TRAP_BR,
	TRAP_UD,
	TRAP_NM,
	TRAP_DF,
	TRAP_CSO, /*reserved,Coprocessor Segment Overrun*/
	TRAP_TS,
	TRAP_NP,
	TRAP_SS,
	TRAP_GP,
	TRAP_PF,
	TRAP_RESERVED,
	TRAP_MF,
	TRAP_AC,
	TRAP_MC,
	TRAP_XM,
	TRAP_VE,
	TRAP_ARCH_USED,
};
void init_interrupt(void);
void trap_handler();
#endif