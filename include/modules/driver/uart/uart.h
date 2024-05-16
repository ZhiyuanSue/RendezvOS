#ifndef _UART_H_
#define	_UART_H_
#include <common/types.h>

#ifdef	_UART_16550A_
#include "uart_16550A.h"
#elif	defined	_UART_PL011_
#include "uart_pl011.h"
#endif

void uart_open(void* base_addr);
void uart_putc(u_int8_t ch);
u_int8_t uart_getc();
void uart_close();

#endif