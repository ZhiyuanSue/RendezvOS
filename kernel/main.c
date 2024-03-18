#include <shampoos/common.h>
#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>
void cmain(struct setup_info* arch_setup_info){
	uart_16550A_open();
	log_init((void*)(arch_setup_info->log_buffer_addr),&uart_16550A_putc);
	printk("try to print %x\n",0x10086);
	uart_16550A_putc('\n');
	uart_16550A_putc('S');
	uart_16550A_putc('h');
	uart_16550A_putc('a');
	uart_16550A_putc('m');
	uart_16550A_putc('P');
	uart_16550A_putc('o');
	uart_16550A_putc('O');
	uart_16550A_putc('S');
	uart_16550A_putc('\n');
	start_arch(arch_setup_info);
}