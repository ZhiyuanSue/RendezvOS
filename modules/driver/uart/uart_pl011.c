#ifdef _UART_PL011_
#include <modules/driver/uart/uart_pl011.h>
UART_PL011* pl011;
void uart_pl011_open(void* base_addr)
{
	pl011=(UART_PL011*)base_addr;
	pl011->ICR=0x7ff;
	pl011->IFLS=0;
	pl011->IMSC=1<<4;
	pl011->CR=(1<<0)|(1<<8)|(1<<9);
}
void uart_pl011_putc(u_int8_t ch)
{
	while((pl011->FR)&(1<<5))	;
	pl011->DR=(uint32_t)ch;
}
u_int8_t uart_pl011_getc()
{
	return 0;
}
void uart_pl011_close()
{

}
#endif