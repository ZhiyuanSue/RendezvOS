#ifndef	_SHAMPOOS_TRAP_H_
#define	_SHAMPOOS_TRAP_H_
#include <common/types.h>
#define	IDT_LIMIT	256
void init_idt(void);
void trap_handler();
#endif