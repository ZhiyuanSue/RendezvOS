#include <shampoos/common.h>
#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>
void cmain(struct setup_info* arch_setup_info){
	uart_16550A_open();
	char* test_s="string ok";
	log_init((void*)(arch_setup_info->log_buffer_addr),LOG_DEBUG,&uart_16550A_putc);
	print("ShamPoOS\n");
	pr_info("test printk s %s\n",test_s);
	pr_debug("test printk x %x\n",0xffffffffc0001234);
	pr_error("test printk x %x\n",23456);
	pr_warn("test printk u %u\n",34567);
	start_arch(arch_setup_info);
}