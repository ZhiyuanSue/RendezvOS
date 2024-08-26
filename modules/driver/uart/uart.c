#include <modules/driver/driver.h>

void	uart_open(void *base_addr)
{
#ifdef _UART_16550A_
	uart_16550A_open();
#elif defined _UART_PL011_
	uart_pl011_open(base_addr);
#endif
}

void	uart_putc(u_int8_t ch)
{
#ifdef _UART_16550A_
	uart_16550A_putc(ch);
#elif defined _UART_PL011_
	uart_pl011_putc(ch);
#endif
}

u_int8_t	uart_getc(void)
{
#ifdef _UART_16550A_
	return (uart_16550A_getc());
#elif defined _UART_PL011_
	return (uart_pl011_getc());
#endif
}
void	uart_close(void)
{
#ifdef _UART_16550A_
	uart_16550A_close();
#elif defined _UART_PL011_
	uart_pl011_close();
#endif

}