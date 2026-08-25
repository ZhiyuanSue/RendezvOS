#ifdef _UART_PL011_
#include <modules/driver/uart/uart_pl011.h>
UART_PL011 *pl011;
void uart_pl011_open(void *base_addr)
{
        pl011 = (UART_PL011 *)base_addr;
        pl011->ICR = 0x7ff;
        pl011->IFLS = 0;
        /* RXIM: device may assert; GIC SPI still masked → CPU uses poll getc. */
        pl011->IMSC = 1 << 4;
        pl011->CR = (1 << 0) | (1 << 8) | (1 << 9);
}
void uart_pl011_putc(u8 ch)
{
        while ((pl011->FR) & (1 << 5))
                ;
        pl011->DR = (u32)ch;
}
u8 uart_pl011_getc(void)
{
        /* Poll until RXFE clear, then read DR. */
        while ((pl011->FR) & (1 << 4))
                ;
        return (u8)(pl011->DR & 0xff);
}
void uart_pl011_close(void)
{
}
#endif