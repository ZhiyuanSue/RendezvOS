#ifndef _UART_PL011_H_
#define _UART_PL011_H_
#include <common/types.h>

typedef struct uart_PL011_regs {
        volatile u32 DR;
        union {
                volatile u32 RSR;
                volatile u32 ECR;
        };
        u32 RES0[4];
        volatile u32 FR;
        u32 RES1;
        volatile u32 ILPR;
        volatile u32 IBRD;
        volatile u32 FBRD;
        volatile u32 LCR_H;
        volatile u32 CR;
        volatile u32 IFLS;
        volatile u32 IMSC;
        volatile u32 RIS;
        volatile u32 MIS;
        volatile u32 ICR;
        volatile u32 DMACR;
} UART_PL011;
#define PERIPH_OFFSET 0xFE0
typedef struct uart_pl011_periph {
        volatile u32 ID0;
        volatile u32 ID1;
        volatile u32 ID2;
        volatile u32 ID3;
} UART_PL011_PERIPH;

#define PCELL_OFFSET 0xFF0
typedef struct uart_pl011_pcell {
        volatile u32 ID0;
        volatile u32 ID1;
        volatile u32 ID2;
        volatile u32 ID3;
} UART_PL011_PCELL;

void uart_pl011_open(void *base_addr);
void uart_pl011_putc(u_int8_t ch);
u_int8_t uart_pl011_getc(void);
void uart_pl011_close(void);

#endif