#ifndef _UART_H_
#define _UART_H_
#include <common/types.h>

/**
 * @brief Open the UART backend (16550 or PL011).
 *
 * x86 16550: @p base_addr is ignored (fixed COM1). aarch64 PL011: @p base_addr
 * is an already-mapped MMIO virtual address.
 */
void uart_open(void *base_addr);
/**
 * @brief Polling transmit one byte.
 */
void uart_putc(u_int8_t ch);
/**
 * @brief Blocking poll receive until data ready; no timeout.
 */
u_int8_t uart_getc(void);
/**
 * @brief Close the UART backend.
 */
void uart_close(void);
/**
 * @brief send ANSI CSI color sequence (\\033[fg;bgm).
 */
void uart_set_color(u64 forword_color, u64 backword_color);

#endif
