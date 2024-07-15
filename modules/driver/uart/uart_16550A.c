#ifdef _UART_16550A_
/*
 * The Qemu virt device use 16550A, so we have to build an uart driver of 16550A
 * You can also visit http://byterunner.com/16550.html
 * and 16550A is totally compatible with 8250 of intel
 */

#include <modules/driver/uart/uart_16550A.h>

/*The following functions are just used for early print*/
void uart_16550A_open() {
	/*set 115200*/

	/*
		Baud rate	DLM		DLL
		50			09H		00H
		75			06H		00H
		110			04H		17H
		300			01H		80H
		600			00H		C0H
		1200		00H		60H
		1800		00H		40H
		2400		00H		30H
		3600		00H		20H
		4800		00H		18H
		7200		00H		10H
		9600		00H		0CH
		11520		00H		0AH
		19200		00H		06H
		23040		00H		05H
		57600		00H		02H
		115200		00H		01H
	*/
	uart_write_reg(IER, 0x00);
	u_int8_t lcr = uart_read_reg(LCR);
	uart_write_reg(LCR, lcr | (1 << 7));
	uart_write_reg(DLM, 0x0);
	uart_write_reg(DLL, 0x01);
	uart_write_reg(LCR, 0);
}
void uart_16550A_putc(u_int8_t ch) {
	while (!(uart_read_reg(LSR) & (1 << 5)))
		;
	uart_write_reg(THR, ch);
}
u_int8_t uart_16550A_getc() { return 0; }
void uart_16550A_close() {}

#endif