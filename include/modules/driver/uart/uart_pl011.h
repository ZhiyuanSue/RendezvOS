#ifndef _UART_PL011_H_
#define _UART_PL011_H_
#include <common/types.h>

void uart_pl011_open();
void uart_pl011_putc(u_int8_t ch);
u_int8_t uart_pl011_getc();
void uart_pl011_close();

#endif