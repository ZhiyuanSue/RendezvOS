#ifndef _UART_PL011_H_
#define _UART_PL011_H_
#include <common/types.h>

typedef struct uart_PL011_regs {
    volatile uint32_t DR;
    union {
        volatile uint32_t RSR;
        volatile uint32_t ECR;
    };
    uint32_t          RES0[ 4 ];
    volatile uint32_t FR;
    uint32_t          RES1;
    volatile uint32_t ILPR;
    volatile uint32_t IBRD;
    volatile uint32_t FBRD;
    volatile uint32_t LCR_H;
    volatile uint32_t CR;
    volatile uint32_t IFLS;
    volatile uint32_t IMSC;
    volatile uint32_t RIS;
    volatile uint32_t MIS;
    volatile uint32_t ICR;
    volatile uint32_t DMACR;
} UART_PL011;
#define PERIPH_OFFSET 0xFE0
typedef struct uart_pl011_periph {
    volatile uint32_t ID0;
    volatile uint32_t ID1;
    volatile uint32_t ID2;
    volatile uint32_t ID3;
} UART_PL011_PERIPH;

#define PCELL_OFFSET 0xFF0
typedef struct uart_pl011_pcell {
    volatile uint32_t ID0;
    volatile uint32_t ID1;
    volatile uint32_t ID2;
    volatile uint32_t ID3;
} UART_PL011_PCELL;

void     uart_pl011_open(void *base_addr);
void     uart_pl011_putc(u_int8_t ch);
u_int8_t uart_pl011_getc(void);
void     uart_pl011_close(void);

#endif