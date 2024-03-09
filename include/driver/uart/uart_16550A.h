#ifndef _UART_16550A_H_
#define _UART_16550A_H_
#include <shampoos/common.h>
#include <shampoos/stdbool.h>

typedef struct UART_16550A_regs{
	union
	{
		volatile u_int8_t RHR;
		volatile u_int8_t THR;
	volatile u_int8_t DLL;
	};
	union{
		volatile u_int8_t IER;
		volatile u_int8_t DLM;
	};
	union 
	{
		volatile u_int8_t FCR;
		volatile u_int8_t ISR;
	};
	volatile u_int8_t LCR;
	volatile u_int8_t MCR;
	volatile u_int8_t LSR;
	volatile u_int8_t MSR;
	volatile u_int8_t SPR;
}UART_16550A;
/* At the beginning, the states are
 * IER = 0
 * ISR = 1
 * LCR = 0
 * MCR = 0
 * LSR = 60 HEX
 * MSR = BITS 0-3 = 0, BITS 4-7 = inputs
 * FCR = 0
 * TX = High
 * OP1 = High
 * OP2 = High
 * RTS = High
 * DTR = High
 * RXRDY = High
 * TXRDY = Low
 * INT = Low
 */
void uart_16550A_open();
void uart_16550A_putc(u_int8_t ch);
u_int8_t uart_16550A_getc();
void uart_16550A_close();


#if _I386_ || _X86_64_
	#include <shampoos/io.h>
	#include <shampoos/stddef.h>
	#define uart_write_reg(reg_name,data)   \
		outb(_X86_16550A_COM1_BASE_+offsetof(UART_16550A,reg_name) ,data)
	#define uart_read_reg(reg_name) \
		inb(_X86_16550A_COM1_BASE_+offsetof(UART_16550A,reg_name))

#elif _RISCV64_
	#define _VIRT_BASE_COM0_ 0x10000000UL
	#define uart_write_reg(reg_name,data)   \
		((((UART_16550A*)_VIRT_BASE_COM0_)->reg_name=data))
	#define uart_read_reg(reg_name) \
		(((UART_16550A*)_VIRT_BASE_COM0_)->reg_name)

#endif

#endif