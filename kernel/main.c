#include <shampoos/common.h>
#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>

extern int log_level;


void cmain(struct setup_info* arch_setup_info){
	uart_16550A_open();
	log_init((void*)(arch_setup_info->log_buffer_addr),log_level,&uart_16550A_putc);
	pr_info("ShamPoOS\n");

	start_arch(arch_setup_info);
}
