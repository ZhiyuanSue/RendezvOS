#ifndef _UART_H_
#define _UART_H_
#include <common/types.h>

void uart_open(void *base_addr);
void uart_putc(u_int8_t ch);
u_int8_t uart_getc(void);
void uart_close(void);
void uart_set_color(u64 forword_color, u64 backword_color);

#endif