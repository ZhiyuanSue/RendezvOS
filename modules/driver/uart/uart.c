#include <modules/driver/driver.h>

void uart_open(void *base_addr)
{
        (void)base_addr;
#ifdef _UART_16550A_
        uart_16550A_open();
#elif defined _UART_PL011_
        uart_pl011_open(base_addr);
#endif
}

void uart_putc(u_int8_t ch)
{
#ifdef _UART_16550A_
        uart_16550A_putc(ch);
#elif defined _UART_PL011_
        uart_pl011_putc(ch);
#endif
}

u_int8_t uart_getc(void)
{
#ifdef _UART_16550A_
        return (uart_16550A_getc());
#elif defined _UART_PL011_
        return (uart_pl011_getc());
#endif
}
void uart_close(void)
{
#ifdef _UART_16550A_
        uart_16550A_close();
#elif defined _UART_PL011_
        uart_pl011_close();
#endif
}
void uart_set_color(u64 forword_color, u64 backword_color)
{
        uart_putc('\33');
        uart_putc('[');
        switch (forword_color) {
        case 0:
                uart_putc('0');
                break;
        case 30:
                uart_putc('3');
                uart_putc('0');
                break;
        case 31:
                uart_putc('3');
                uart_putc('1');
                break;
        case 32:
                uart_putc('3');
                uart_putc('2');
                break;
        case 33:
                uart_putc('3');
                uart_putc('3');
                break;
        case 34:
                uart_putc('3');
                uart_putc('4');
                break;
        default:
                break;
        }
        uart_putc(';');
        switch (backword_color) {
        case 0:
                uart_putc('0');
                break;
        case 30:
                uart_putc('3');
                uart_putc('0');
                break;
        case 31:
                uart_putc('3');
                uart_putc('1');
                break;
        case 32:
                uart_putc('3');
                uart_putc('2');
                break;
        case 33:
                uart_putc('3');
                uart_putc('3');
                break;
        case 34:
                uart_putc('3');
                uart_putc('4');
                break;
        case 40:
                uart_putc('4');
                uart_putc('0');
                break;
        default:
                break;
        }
        uart_putc('m');
}